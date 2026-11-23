#include "aio.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/atomic.hpp>

#include "dispatch.hpp"
#include "memory.hpp"
#include "ring.hpp"

namespace astia::io::aio {

    namespace {
        user_memory_adapter g_user_memory{};
    }

    void set_user_memory_adapter(const user_memory_adapter& adapter) {
        g_user_memory = adapter;
    }

    const user_memory_adapter& user_memory() {
        return g_user_memory;
    }

    using namespace ring;

    long io_setup(u32 entries, u32 flags, astia_rings** out) {
        return ring::setup(entries, flags, out);
    }

    long io_destroy(astia_rings* ring) {
        return ring::destroy(ring);
    }

    long io_submit(astia_rings* ring, u32 to_submit) {
        io_process_deferred_ready();
        ring_guard pool_guard(g_ring_pool_lock);
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr || !ring_layout_ok(*slot, ring)) {
            return -EINVAL;
        }
        ring_guard ring_lock_guard(slot->lock);
        // The ring lock pins the slot while the submitter runs.  The pool is
        // needed only for lookup/lifetime, not for dispatch or CQ work.
        pool_guard.release_lock();

        dispatch::retry_pending(*slot, rings_of(*slot));

        astia_sq_ring* sq = astia_rings_sq(ring);
        const u32 tail = astia::atomic_ref(sq->tail).load(astia::memory_order::acquire);
        u32       head = astia::atomic_ref(sq->head).load(astia::memory_order::relaxed);
        u32       consumed = 0;
        while (consumed < to_submit && head != tail) {
            const astia_sqe s = astia_rings_sqes(ring)[head & sq->mask];
            if (!dispatch::dispatch_one(*slot, ring, s)) {
                break;
            }
            head += 1;
            astia::atomic_ref(sq->head).store(head, astia::memory_order::release);
            consumed += 1;
        }

        // Inline completions and pin failures use park storage so their
        // resource release is deferred just like an IRQ completion.  Detach
        // them while protected, then release after unlocking below.
        // This single cleanup generation also reclaims completions left by a
        // previous enter.  Keeping an earlier generation alive on this small
        // kernel stack would both overwrite resources when slots are reused
        // and exceed the 16 KiB stack budget.
        parked_cleanup& cleanup = slot->cleanup;
        reap_completed(*slot, cleanup);
        cq_retrigger(*slot, rings_of(*slot));

        ring_lock_guard.unlock();
        cleanup.release();
        return static_cast<long>(consumed);
    }

    bool io_allow_kernel_buffers(astia_rings* ring) {
        ring_guard pool_guard(g_ring_pool_lock);
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr || slot->owner.get() != proc::current_process())
            return false;
        ring_guard ring_lock_guard(slot->lock);
        pool_guard.release_lock();
        slot->kernel_buffers = true;
        return true;
    }

    void io_device_ready(i32 fd) {
        proc::process* current        = proc::current_process();
        auto           current_object = current == nullptr ? mm::shptr<fs::open_file>{}
                                                           : current->descriptor(fd_t{fd});
        if (!current_object) {
            return;
        }
        io_device_ready_context(current_object->node().get());
    }

    void io_device_ready_context(const io_device* dev) {
        io_device_ready_context_except(dev, ~u64{0});
    }

    void io_device_ready_context_except(const io_device* dev, u64 excluded_user_data) {
        if (dev == nullptr) {
            return;
        }
        note_device_ready(dev);
        bool indexed[kernel_ring_slot_count][parked_operation_slots]{};
        snapshot_waiters(dev, indexed);
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                if (!indexed[i][p])
                    continue;
                ring_guard pool_guard(g_ring_pool_lock);
                ring_slot* slot_ptr = g_slots[i];
                if (slot_ptr == nullptr || !slot_ptr->used)
                    continue;
                ring_slot& slot = *slot_ptr;
                ring_guard ring_lock_guard(slot.lock);
                pool_guard.release_lock();
                parked_op& op = slot.park[p];
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    op.object && op.object->node().get() == dev &&
                    op.operation->request().user_data() != excluded_user_data)
                    op.device_ready = true;
            }
        }
        if (::astia::arch::cpu::in_interrupt())
            ::astia::arch::cpu::request_local_reschedule();
    }

    namespace {
        /// A process that exited with detached operations left the ring in
        /// place until they finish.  Once the last one publishes, tear the
        /// slot down from task context.
        void reclaim_detached_slots() {
            for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
                ring_guard pool_guard(g_ring_pool_lock);
                ring_slot* slot = g_slots[i];
                if (slot == nullptr || !slot->used || !slot->detached_reclaim)
                    continue;
                ring_guard ring_lock_guard(slot->lock);
                pool_guard.release_lock();
                if (slot->detached_ops.load(astia::memory_order::acquire) != 0)
                    continue;
                slot->reclaiming.store(true, astia::memory_order::release);
                parked_cleanup& cleanup = slot->cleanup;
                reap_completed(*slot, cleanup);
                unmap_ring_user(*slot, slot->owner.get());
                clear_slot(*slot, cleanup);
                ring_lock_guard.unlock();
                cleanup.release();
                {
                    ring_guard pool_flip(g_ring_pool_lock);
                    slot->used = false;
                    slot->reclaiming.store(false, astia::memory_order::release);
                    g_slots[i] = nullptr;
                }
            }
        }
    } // namespace

    void io_process_deferred_ready() {
        reclaim_detached_slots();
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                ring_guard pool_guard(g_ring_pool_lock);
                ring_slot* slot_ptr = g_slots[i];
                if (slot_ptr == nullptr || !slot_ptr->used || !slot_ptr->owner)
                    continue;
                ring_slot& slot = *slot_ptr;
                ring_guard ring_lock_guard(slot.lock);
                pool_guard.release_lock();
                parked_op&     op    = slot.park[p];
                proc::process* owner = slot.owner.get();
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    (op.device_ready || op.sleep_ready ||
                     op.operation->request().timeout_expired() ||
                     op.operation->request().poll_wakeup())) {
                    op.device_ready = false;
                    if (op.sleep_ready) {
                        op.sleep_ready = false;
                        dispatch::complete_control(owner, rings_of(slot), op, 0);
                    }
                    else {
                        dispatch::resume_parked(owner, rings_of(slot), op);
                    }
                }
                cq_retrigger(slot, rings_of(slot));
            }
        }
    }

    void io_interrupt_process(proc::process* owner, i32 result) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used || slot_ptr->owner.get() != owner)
                continue;
            ring_slot& slot = *slot_ptr;
            ring_guard ring_lock_guard(slot.lock);
            pool_guard.release_lock();
            astia_rings* ring = rings_of(slot);
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    op.signal_interruptible)
                    dispatch::complete_control(owner, ring, op, result);
            }
            cq_retrigger(slot, ring);
        }
    }

    void io_poll_tick() {
        if (::astia::arch::timer_hz() == 0)
            return;
        const u64 now = dispatch::poll_now_ns();
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used)
                continue;
            ring_slot& slot = *slot_ptr;
            ring_guard ring_lock_guard(slot.lock);
            pool_guard.release_lock();
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    op.sleeping && now >= op.sleep_deadline) {
                    op.sleep_ready = true;
                }
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    op.timed && now >= op.timeout_deadline) {
                    op.timed = false;
                    op.operation->request().timeout_expired(true);
                }
                if (op.used && op.type == parked_op::kind::operation && !op.done &&
                    op.poll_timed && now >= op.poll_deadline) {
                    op.poll_timed = false;
                    op.operation->request().poll_wakeup(true);
                }
            }
        }
        ::astia::arch::cpu::request_local_reschedule();
    }

    void io_exec_process(proc::process* owner) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner.get() != owner)
                continue;
            ring_guard ring_lock_guard(slot->lock);
            pool_guard.release_lock();
            if (slot->detached_ops.load(astia::memory_order::acquire) != 0) {
                slot->detached_reclaim = true;
                continue;
            }
            slot->reclaiming.store(true, astia::memory_order::release);
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            ring_lock_guard.unlock();
            cleanup.release();
            ring_guard pool_flip(g_ring_pool_lock);
            slot->used = false;
            slot->reclaiming.store(false, astia::memory_order::release);
        }
    }

    void io_exit_process(proc::process* owner) {
        if (owner == nullptr)
            return;
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot = g_slots[i];
            if (slot == nullptr || !slot->used || slot->owner.get() != owner)
                continue;
            ring_guard ring_lock_guard(slot->lock);
            pool_guard.release_lock();
            if (slot->detached_ops.load(astia::memory_order::acquire) != 0) {
                slot->detached_reclaim = true;
                continue;
            }
            slot->reclaiming.store(true, astia::memory_order::release);
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            // No operation may outlive its process.  In particular, a parked
            // pipe read holds an open_file reference; leaving the ring slot
            // behind until waitpid would keep that endpoint alive and prevent
            // EOF from reaching the other end.  The process is gone, so cancel
            // all parked operations while the slot is still owned by it.
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            ring_lock_guard.unlock();
            cleanup.release();
            {
                ring_guard pool_flip(g_ring_pool_lock);
                slot->used = false;
                slot->reclaiming.store(false, astia::memory_order::release);
                g_slots[i] = nullptr;
            }
        }
    }

} // namespace astia::io::aio
