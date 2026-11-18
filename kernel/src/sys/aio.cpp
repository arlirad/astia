#include "aio.hpp"
#include "aio_ring.hpp"
#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include "aio_dispatch.hpp"

namespace astia::sys {

    using namespace aio_ring;

    long io_setup(u32 entries, u32 flags, astia_rings** out) {
        return aio_ring::setup(entries, flags, out);
    }

    long io_destroy(astia_rings* ring) {
        return aio_ring::destroy(ring);
    }

    long io_submit(astia_rings* ring, u32 to_submit) {
        ring_guard pool_guard(g_ring_pool_lock);
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr || !ring_layout_ok(*slot, ring)) {
            return -EINVAL;
        }
        ring_guard ring_lock_guard(slot->lock);
        // The ring lock pins the slot while the submitter runs.  The pool is
        // needed only for lookup/lifetime, not for dispatch or CQ work.
        pool_guard.release_lock();

        aio_dispatch::retry_pending(*slot, rings_of(*slot));

        astia_sq_ring* sq       = astia_rings_sq(ring);
        const u32      tail     = __atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE);
        u32            head     = __atomic_load_n(&sq->head, __ATOMIC_RELAXED);
        u32            consumed = 0;
        while (consumed < to_submit && head != tail) {
            const astia_sqe s = astia_rings_sqes(ring)[head & sq->mask];
            if (!aio_dispatch::dispatch_one(*slot, ring, s)) {
                break;
            }
            head += 1;
            __atomic_store_n(&sq->head, head, __ATOMIC_RELEASE);
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

    void io_device_ready(i32 fd) {
        proc::process* current = proc::current_process();
        auto current_object    = current == nullptr ? mm::shptr<fs::open_file>{}
                                                    : current->descriptor(fd_t{fd});
        if (!current_object) {
            return;
        }
        const fs::file* device = current_object->node().get();
        bool            indexed[kernel_ring_slot_count][parked_operation_slots]{};
        note_device_ready(device);
        snapshot_waiters(device, indexed);
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
                if (op.used && !op.done && op.fd == fd && op.object &&
                    op.object->node().get() == device) {
                    aio_dispatch::resume_parked(rings_of(slot), op);
                }
                cq_retrigger(slot, rings_of(slot));
            }
        }
    }

    void io_device_ready_context(const io_device* dev) {
        io_device_ready_context_except(dev, ~u64{0});
    }

    void io_device_ready_context_except(const io_device* dev, u64 excluded_user_data) {
        if (dev == nullptr) {
            return;
        }
        bool indexed[kernel_ring_slot_count][parked_operation_slots]{};
        note_device_ready(dev);
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
                if (op.used && !op.done && op.object &&
                    op.request.user_data() != excluded_user_data &&
                    op.object->node().get() == dev) {
                    aio_dispatch::resume_parked(rings_of(slot), op);
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
                if (op.used && !op.done && op.signal_interruptible)
                    aio_dispatch::complete_control(owner, ring, op, result);
            }
            cq_retrigger(slot, ring);
        }
    }

    void io_poll_tick() {
        if (arch::timer_hz() == 0)
            return;
        const u64 now = aio_dispatch::poll_now_ns();
        for (u32 i = 0; i < kernel_ring_slot_count; ++i) {
            ring_guard pool_guard(g_ring_pool_lock);
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr == nullptr || !slot_ptr->used)
                continue;
            ring_slot& slot = *slot_ptr;
            ring_guard ring_lock_guard(slot.lock);
            pool_guard.release_lock();
            astia_rings* ring = rings_of(slot);
            aio_dispatch::retry_pending(slot, ring);
            for (u32 p = 0; p < parked_operation_slots; ++p) {
                parked_op& op = slot.park[p];
                if (op.used && !op.done && op.sleeping && now >= op.sleep_deadline)
                    aio_dispatch::complete_control(nullptr, ring, op, 0);
                if (op.used && !op.done && op.timed && now >= op.timeout_deadline) {
                    op.timed = false;
                    op.request.timeout_expired(true);
                    aio_dispatch::resume_parked(ring, op);
                }
                if (op.used && !op.done && op.poll_timed && now >= op.poll_deadline) {
                    op.poll_timed = false;
                    op.request.poll_wakeup(true);
                    aio_dispatch::resume_parked(ring, op);
                }
            }
            cq_retrigger(slot, ring);
        }
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
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            ring_lock_guard.unlock();
            cleanup.release();
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
            parked_cleanup& cleanup = slot->cleanup;
            reap_completed(*slot, cleanup);
            // No operation may outlive its process.  In particular, a parked
            // pipe read holds an open_file reference; leaving the ring slot
            // behind until waitpid would keep that endpoint alive and prevent
            // EOF from reaching the other end.  The process is gone, so cancel
            // all parked operations while the slot is still owned by it.
            unmap_ring_user(*slot, owner);
            clear_slot(*slot, cleanup);
            g_slots[i] = nullptr;
            ring_lock_guard.unlock();
            cleanup.release();
            slot->~ring_slot();
        }
    }

} // namespace astia::sys
