#include "aio_ring.hpp"

#include "user_mem.hpp"

#include <astia/api/errno.h>
#include <astia/arch/page_map.hpp>
#include <astia/memory.hpp>

namespace astia::sys::aio_ring {

    static_assert(alignof(ring_slot) >= mm::page_size);
    static_assert(mapping_bytes <= sizeof(ring_slot::map));

    ring_slot* g_slots[slot_count];
    ring_lock  g_ring_pool_lock;
    alignas(mm::page_size) u8 slot_storage[slot_count][sizeof(ring_slot)];

    namespace {
        constexpr u32 waiter_index_buckets = 32;
        constexpr u16 waiter_index_none    = 0xffff;

        struct waiter_index_entry {
            const fs::file* key  = nullptr;
            u16             next = waiter_index_none;
        };

        core::spinlock g_waiter_bucket_locks[waiter_index_buckets];
        u16 g_waiter_index_heads[waiter_index_buckets] = {
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none,
            waiter_index_none, waiter_index_none, waiter_index_none, waiter_index_none};
        waiter_index_entry g_waiter_index[slot_count * parked_slot_count];
        u64 g_ready_generation = 0;


        u32 waiter_index_hash(const fs::file* key) {
            return (static_cast<u32>(reinterpret_cast<u64>(key) >> 4) &
                    (waiter_index_buckets - 1));
        }

        u32 waiter_index_number(u32 slot, u32 park_index) {
            return slot * parked_slot_count + park_index;
        }
    }

    void ring_lock::acquire() {
        const u32 cpu = arch::cpu::current_index();
        if (cpu < cpu_slots && depth[cpu] != 0) {
            ++depth[cpu];
            return;
        }
        lock.lock();
        if (cpu < cpu_slots)
            depth[cpu] = 1;
    }

    void ring_lock::release() {
        const u32 cpu = arch::cpu::current_index();
        if (cpu < cpu_slots && --depth[cpu] != 0)
            return;
        lock.unlock();
    }

    ring_guard::ring_guard(ring_lock& lock) : gate_(), lock_(lock) {
        lock_.acquire();
    }

    ring_guard::~ring_guard() {
        if (held_)
            lock_.release();
        gate_.unlock();
    }

    void ring_guard::unlock() {
        if (held_) {
            lock_.release();
            held_ = false;
        }
        gate_.unlock();
    }

    void ring_guard::release_lock() {
        if (held_) {
            lock_.release();
            held_ = false;
        }
    }

    void parked_cleanup::release() {
        for (u32 i = 0; i < parked_slot_count; ++i) {
            entries[i].release();
            objects[i] = mm::shptr<fs::open_file>{};
        }
    }

    void waiter_index_insert(const ring_slot& slot, u32 park_index,
                             const fs::file* key) {
        const u32 number = waiter_index_number(slot.pool_index, park_index);
        const u32 bucket = waiter_index_hash(key);
        g_waiter_bucket_locks[bucket].lock();
        g_waiter_index[number].key   = key;
        g_waiter_index[number].next  = g_waiter_index_heads[bucket];
        g_waiter_index_heads[bucket] = static_cast<u16>(number);
        g_waiter_bucket_locks[bucket].unlock();
    }

    void waiter_index_remove(const ring_slot& slot, u32 park_index) {
        const u32 number = waiter_index_number(slot.pool_index, park_index);
        const fs::file* key = g_waiter_index[number].key;
        if (key == nullptr)
            return;
        const u32 bucket = waiter_index_hash(key);
        g_waiter_bucket_locks[bucket].lock();
        u16* link = &g_waiter_index_heads[bucket];
        while (*link != waiter_index_none) {
            if (*link == number) {
                *link = g_waiter_index[number].next;
                break;
            }
            link = &g_waiter_index[*link].next;
        }
        g_waiter_index[number] = {};
        g_waiter_bucket_locks[bucket].unlock();
    }

    u64 ready_generation() {
        return __atomic_load_n(&g_ready_generation, __ATOMIC_ACQUIRE);
    }

    void note_device_ready(const fs::file*) {
        __atomic_fetch_add(&g_ready_generation, 1, __ATOMIC_ACQ_REL);
    }

    void clear_waiter_hint(const ring_slot& slot, u32 park_index) {
        waiter_index_remove(slot, park_index);
    }

    void publish_waiter_hint(const ring_slot& slot, u32 park_index,
                             const fs::file* key) {
        waiter_index_insert(slot, park_index, key);
    }

    void snapshot_waiters(const fs::file* key,
                          bool (&indexed)[slot_count][parked_slot_count]) {
        arch::interrupt_guard index_gate;
        const u32 bucket = waiter_index_hash(key);
        g_waiter_bucket_locks[bucket].lock();
        for (u16 n = g_waiter_index_heads[bucket]; n != waiter_index_none;
             n = g_waiter_index[n].next)
            if (g_waiter_index[n].key == key)
                indexed[n / parked_slot_count][n % parked_slot_count] = true;
        g_waiter_bucket_locks[bucket].unlock();
    }

    astia_rings* rings_of(const ring_slot& slot) {
        return reinterpret_cast<astia_rings*>(const_cast<u8*>(slot.map));
    }

    ring_slot* slot_of(const astia_rings* ring) {
        const u64      address = reinterpret_cast<u64>(ring);
        proc::process* caller  = proc::current_process();
        for (u32 i = 0; i < slot_count; ++i) {
            if (g_slots[i] != nullptr && g_slots[i]->used &&
                (((caller == nullptr || caller->address_space() == nullptr) &&
                  reinterpret_cast<u64>(rings_of(*g_slots[i])) == address) ||
                 (g_slots[i]->owner.get() == caller && g_slots[i]->user_base != 0 &&
                  address == g_slots[i]->user_base +
                                 (reinterpret_cast<u64>(rings_of(*g_slots[i])) &
                                  mm::page_mask))))
                return g_slots[i];
        }
        return nullptr;
    }

    void detach_parked(ring_slot& slot, parked_cleanup& cleanup, bool completed_only) {
        for (u32 i = 0; i < parked_slot_count; ++i) {
            parked_op& op = slot.park[i];
            if (!op.used || (completed_only && !op.done))
                continue;
            clear_waiter_hint(slot, i);
            cleanup.entries[i] = static_cast<mm::iovec&&>(op.entries);
            cleanup.objects[i] = static_cast<mm::shptr<fs::open_file>&&>(op.object);
            op = parked_op{};
        }
    }

    void clear_slot(ring_slot& slot, parked_cleanup& cleanup) {
        for (u32 i = 0; i < parked_slot_count; ++i)
            if (slot.park[i].used && !slot.park[i].done)
                slot.park[i].request.cancel_device();
        detach_parked(slot, cleanup);
        __builtin_memset(slot.map, 0, sizeof slot.map);
        slot.user_base       = 0;
        slot.sq_entries      = 0;
        slot.cq_entries      = 0;
        slot.waiters         = 0;
        slot.notify_pending  = false;
        slot.notify_signaled = 0;
        slot.owner           = {};
        slot.used            = false;
    }

    void reap_completed(ring_slot& slot, parked_cleanup& cleanup) {
        detach_parked(slot, cleanup, true);
    }

    bool ring_layout_ok(const ring_slot& slot, const astia_rings* ring) {
        if (ring == nullptr || ring->magic != RINGS_MAGIC ||
            ring->version != API_VERSION || ring->features != FEAT_SINGLE_MMAP ||
            ring->sq_entries != slot.sq_entries || ring->cq_entries != slot.cq_entries ||
            ring->sq_off != sizeof(astia_rings) ||
            ring->cq_off != ring->sq_off + sizeof(astia_sq_ring) ||
            ring->sqes_off != ring->cq_off + sizeof(astia_cq_ring) ||
            ring->cqes_off != ring->sqes_off + static_cast<u32>(sizeof(astia_sqe)) *
                                                        slot.sq_entries)
            return false;
        const astia_sq_ring* sq = astia_rings_sq(const_cast<astia_rings*>(ring));
        const astia_cq_ring* cq = astia_rings_cq(const_cast<astia_rings*>(ring));
        if (sq->entries != slot.sq_entries || sq->mask != slot.sq_entries - 1 ||
            cq->entries != slot.cq_entries || cq->mask != slot.cq_entries - 1)
            return false;
        const u32 sq_tail = __atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE);
        const u32 sq_head = __atomic_load_n(&sq->head, __ATOMIC_RELAXED);
        const u32 cq_tail = __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE);
        const u32 cq_head = __atomic_load_n(&cq->head, __ATOMIC_RELAXED);
        return sq_tail - sq_head <= slot.sq_entries && cq_tail - cq_head <= slot.cq_entries;
    }

    long map_ring_for_user(ring_slot& slot, u32 index, proc::process* owner, u64* out) {
        arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        const arch::page_map* kernel = arch::kernel_page_map();
        if (space == nullptr || kernel == nullptr)
            return -ESRCH;
        const u64 base  = proc::user_ring_base + proc::user_ring_slot_span * index;
        const u64 first = reinterpret_cast<u64>(rings_of(slot));
        for (u32 page = 0; page < user_mapping_pages; ++page) {
            const u64 offset = static_cast<u64>(page) * mm::page_size;
            const u64 phys = kernel->translate(first + offset) & ~mm::page_mask;
            if (!space->map_4k(base + offset, phys,
                               arch::page_flags::present | arch::page_flags::writable |
                                   arch::page_flags::user)) {
                for (u32 done = 0; done < page; ++done)
                    (void) space->unmap(base + static_cast<u64>(done) * mm::page_size);
                return -ENOMEM;
            }
        }
        slot.user_base = base;
        *out           = base + (first & mm::page_mask);
        return 0;
    }

    void unmap_ring_user(ring_slot& slot, proc::process* owner) {
        if (slot.user_base == 0)
            return;
        arch::page_map* space = owner == nullptr ? nullptr : owner->address_space();
        if (space != nullptr)
            for (u32 page = 0; page < user_mapping_pages; ++page)
                (void) space->unmap(slot.user_base + static_cast<u64>(page) * mm::page_size);
        slot.user_base = 0;
    }

    void cq_retrigger(ring_slot& slot, astia_rings* ring) {
        astia_cq_ring* cq = astia_rings_cq(ring);
        const u32 acknowledged = __atomic_load_n(&cq->notify_ack, __ATOMIC_ACQUIRE);
        if (slot.notify_pending && acknowledged == slot.notify_signaled)
            slot.notify_pending = false;
        if (!slot.notify_pending && astia_cq_ready(ring) != 0 && slot.owner) {
            const u32 generation =
                __atomic_add_fetch(&cq->notify_generation, 1, __ATOMIC_ACQ_REL);
            slot.notify_pending  = true;
            slot.notify_signaled = generation;
            (void) slot.owner->signals().send(signal_number::aio);
            slot.owner->wake_signal_waiters();
        }
    }

    bool cq_post(astia_rings* ring, u64 user_data, i32 result) {
        ring_slot* storage = nullptr;
        for (u32 i = 0; i < slot_count; ++i)
            if (g_slots[i] != nullptr && g_slots[i]->used && rings_of(*g_slots[i]) == ring) {
                storage = g_slots[i];
                break;
            }
        if (storage == nullptr)
            storage = slot_of(ring);
        if (storage == nullptr)
            return false;
        astia_rings* storage_ring = rings_of(*storage);
        astia_cq_ring* cq = astia_rings_cq(storage_ring);
        const u32 head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
        const u32 tail = __atomic_load_n(&cq->tail, __ATOMIC_RELAXED);
        if (tail - head >= cq->entries)
            return false;
        astia_cqe* c = &astia_rings_cqes(storage_ring)[tail & cq->mask];
        c->user_data = user_data;
        c->result    = result;
        c->flags     = 0;
        __atomic_store_n(&cq->tail, tail + 1, __ATOMIC_RELEASE);
        cq_retrigger(*storage, storage_ring);
        return true;
    }

    bool cq_post_guarded(astia_rings* ring, u64 user_data, i32 result) {
        return cq_post(ring, user_data, result);
    }

    long setup(u32 entries, u32 flags, astia_rings** out) {
        if (out == nullptr || flags != 0 || entries == 0 || entries > kernel_ring_max_entries)
            return -EINVAL;
        u32 sq = 1;
        while (sq < entries)
            sq <<= 1;
        const u32 cq = sq * 2 < 64 ? 64 : sq * 2;

        ring_guard pool_guard(g_ring_pool_lock);
        for (u32 i = 0; i < slot_count; ++i) {
            ring_slot* slot_ptr = g_slots[i];
            if (slot_ptr != nullptr && slot_ptr->used)
                continue;
            if (slot_ptr == nullptr) {
                slot_ptr   = ::new (slot_storage[i]) ring_slot{};
                g_slots[i] = slot_ptr;
            }
            ring_slot& slot = *slot_ptr;
            slot.pool_index = i;
            clear_slot(slot, slot.cleanup);
            slot.cleanup.release();
            slot.used = true;
            proc::process* current = proc::current_process();
            if (current == nullptr) {
                slot.used = false;
                continue;
            }
            slot.owner = proc::retain_process(current->pid());
            if (!slot.owner) {
                slot.used = false;
                continue;
            }
            astia_rings* r = rings_of(slot);
            r->magic = RINGS_MAGIC;
            r->version = API_VERSION;
            r->features = FEAT_SINGLE_MMAP;
            r->sq_entries = sq;
            r->cq_entries = cq;
            r->sq_off = sizeof(astia_rings);
            r->cq_off = r->sq_off + sizeof(astia_sq_ring);
            r->sqes_off = r->cq_off + sizeof(astia_cq_ring);
            r->cqes_off = r->sqes_off + static_cast<u32>(sizeof(astia_sqe)) * sq;
            slot.sq_entries = sq;
            slot.cq_entries = cq;
            astia_sq_ring* sqp = astia_rings_sq(r);
            sqp->mask = sq - 1;
            sqp->entries = sq;
            astia_cq_ring* cqp = astia_rings_cq(r);
            cqp->mask = cq - 1;
            cqp->entries = cq;

            astia_rings* visible = r;
            proc::process* owner = proc::current_process();
            if (owner != nullptr && owner->address_space() != nullptr) {
                u64 user_address = 0;
                const long rc = map_ring_for_user(slot, i, owner, &user_address);
                if (rc < 0) {
                    clear_slot(slot, slot.cleanup);
                    slot.cleanup.release();
                    return rc;
                }
                visible = reinterpret_cast<astia_rings*>(user_address);
            }
            if (!sys::copy_out(span<u8>::bytes(reinterpret_cast<u64>(out), sizeof visible),
                               &visible)) {
                unmap_ring_user(slot, owner);
                clear_slot(slot, slot.cleanup);
                slot.cleanup.release();
                return -EFAULT;
            }
            return 0;
        }
        return -ENFILE;
    }

    long destroy(astia_rings* ring) {
        ring_guard pool_guard(g_ring_pool_lock);
        ring_slot* slot = slot_of(ring);
        if (slot == nullptr || !ring_layout_ok(*slot, ring))
            return -EINVAL;
        ring_guard ring_lock_guard(slot->lock);
        parked_cleanup& cleanup = slot->cleanup;
        reap_completed(*slot, cleanup);
        if (slot->waiters != 0) {
            ring_lock_guard.unlock();
            pool_guard.unlock();
            cleanup.release();
            return -EBUSY;
        }
        for (u32 i = 0; i < parked_slot_count; ++i) {
            if (slot->park[i].used) {
                ring_lock_guard.unlock();
                pool_guard.unlock();
                cleanup.release();
                return -EBUSY;
            }
        }
        for (u32 i = 0; i < slot_count; ++i)
            if (g_slots[i] == slot) {
                g_slots[i] = nullptr;
                break;
            }
        pool_guard.release_lock();
        unmap_ring_user(*slot, proc::current_process());
        clear_slot(*slot, cleanup);
        ring_lock_guard.unlock();
        cleanup.release();
        slot->~ring_slot();
        return 0;
    }

} // namespace astia::sys::aio_ring
