#pragma once

#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/io.hpp>
#include <astia/memory.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/mm/share.hpp>

#include "aio.hpp"
#include "proc.hpp"

namespace astia::sys::aio_ring {

    inline constexpr u32 slot_count = resource_limits::queue::aio_ring_slots;
    inline constexpr u32 parked_slot_count =
        resource_limits::queue::aio_parked_operations;
    inline constexpr u32   kernel_ring_slot_count = slot_count;
    inline constexpr u32   parked_operation_slots = parked_slot_count;
    inline constexpr usize mapping_bytes =
        sizeof(astia_rings) + sizeof(astia_sq_ring) + sizeof(astia_cq_ring) +
        sizeof(astia_sqe) * kernel_ring_max_entries +
        sizeof(astia_cqe) * (2 * kernel_ring_max_entries);
    inline constexpr usize mapping_pages =
        align_up(mapping_bytes, mm::page_size) / mm::page_size;
    inline constexpr u32 user_mapping_pages = static_cast<u32>(mapping_pages);

    struct ring_lock {
        core::spinlock       lock;
        static constexpr u32 cpu_slots = arch::platform::max_cpu_count;
        u32                  depth[cpu_slots]{};

        void acquire();
        void release();
    };

    class ring_guard {
        public:
        explicit ring_guard(ring_lock& lock);
        ~ring_guard();

        ring_guard(const ring_guard&)            = delete;
        ring_guard& operator=(const ring_guard&) = delete;

        void unlock();
        void release_lock();

        private:
        arch::interrupt_guard gate_;
        ring_lock&            lock_;
        bool                  held_ = true;
    };

    struct parked_op {
        bool                     used;
        bool                     done                  = false;
        bool                     resuming              = false;
        bool                     control               = false;
        bool                     signal_interruptible  = false;
        bool                     socket_accept         = false;
        bool                     readiness             = false;
        bool                     sleeping              = false;
        bool                     timed                 = false;
        bool                     poll_timed            = false;
        bool                     sleep_absolute        = false;
        bool                     sleep_ready           = false;
        bool                     device_ready          = false;
        u32                      poll_events           = 0;
        u64                      sleep_deadline        = 0;
        u64                      timeout_deadline      = 0;
        u64                      poll_deadline         = 0;
        u64                      sleep_remaining_user  = 0;
        bool                     recvfrom_address      = false;
        u64                      recvfrom_address_user = 0;
        bool                     pending_cqe           = false;
        u64                      pending_user_data     = 0;
        i32                      pending_result        = 0;
        i32                      fd;
        mm::shptr<fs::open_file> object;
        mm::iovec                entries;
        io::operation*           operation  = nullptr;
        u64                      generation = 0;
        usize                    moved      = 0;

        void clear() {
            if (operation != nullptr)
                operation->release_owner();
            operation             = nullptr;
            used                  = false;
            done                  = false;
            resuming              = false;
            control               = false;
            signal_interruptible  = false;
            socket_accept         = false;
            readiness             = false;
            sleeping              = false;
            timed                 = false;
            poll_timed            = false;
            sleep_absolute        = false;
            sleep_ready           = false;
            device_ready          = false;
            poll_events           = 0;
            sleep_deadline        = 0;
            timeout_deadline      = 0;
            poll_deadline         = 0;
            sleep_remaining_user  = 0;
            recvfrom_address      = false;
            recvfrom_address_user = 0;
            pending_cqe           = false;
            pending_user_data     = 0;
            pending_result        = 0;
            fd                    = 0;
            object                = {};
            generation            = 0;
            moved                 = 0;
        }
    };

    struct parked_cleanup {
        mm::iovec                entries[parked_slot_count];
        mm::shptr<fs::open_file> objects[parked_slot_count];

        void release();
    };

    struct ring_slot {
        ring_lock                lock;
        bool                     used;
        mm::shptr<proc::process> owner;
        u64                      user_base;
        u32                      sq_entries;
        u32                      cq_entries;
        u32                      waiters;
        bool                     notify_pending;
        u32                      notify_signaled;
        u32                      pool_index;
        bool                     kernel_buffers;
        parked_op                park[parked_slot_count];
        parked_cleanup           cleanup;
        alignas(mm::page_size) u8 map[mapping_pages * mm::page_size];
    };

    extern ring_slot* g_slots[slot_count];
    extern ring_lock  g_ring_pool_lock;
    extern u8         slot_storage[slot_count][sizeof(ring_slot)];

    astia_rings* rings_of(const ring_slot& slot);
    ring_slot*   slot_of(const astia_rings* ring);
    bool         ring_layout_ok(const ring_slot& slot, const astia_rings* ring);

    long map_ring_for_user(ring_slot& slot, u32 index, proc::process* owner, u64* out);
    void unmap_ring_user(ring_slot& slot, proc::process* owner);
    void detach_parked(ring_slot& slot, parked_cleanup& cleanup,
                       bool completed_only = false);
    void clear_slot(ring_slot& slot, parked_cleanup& cleanup);
    void reap_completed(ring_slot& slot, parked_cleanup& cleanup);

    bool cq_post(astia_rings* ring, u64 user_data, i32 result);
    bool cq_post_guarded(astia_rings* ring, u64 user_data, i32 result);
    void cq_retrigger(ring_slot& slot, astia_rings* ring);

    u64  ready_generation();
    void note_device_ready(const fs::file* device);
    void clear_waiter_hint(const ring_slot& slot, u32 park_index);
    void publish_waiter_hint(const ring_slot& slot, u32 park_index, const fs::file* key);
    void snapshot_waiters(const fs::file* key,
                          bool (&indexed)[slot_count][parked_slot_count]);

    long setup(u32 entries, u32 flags, astia_rings** out);
    long destroy(astia_rings* ring);

} // namespace astia::sys::aio_ring
