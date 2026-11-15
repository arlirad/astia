#pragma once

#include <astia/common.hpp>

namespace astia::resource_limits {

    namespace abi {
        inline constexpr u32 syscall_table_capacity = 128;
        inline constexpr u32 aio_ring_slots          = 4;
        inline constexpr u32 aio_ring_entries        = 64;
        inline constexpr u32 aio_parked_operations   = 64;
        inline constexpr usize aio_operation_bytes   = 0x7ffff000;
        inline constexpr u32 pci_bar_count           = 6;
        inline constexpr u32 pci_capability_count    = 48;
        inline constexpr u32 pci_extended_capability_count = 32;
    }

    namespace policy {
        inline constexpr u32 process_count                  = 64;
        inline constexpr u32 threads_per_process      = 16;
        inline constexpr u32 process_waiters          = 4;
        inline constexpr u32 supplementary_groups     = 16;
        inline constexpr u32 argv_entries             = 16;
        inline constexpr u32 env_entries              = 16;
        inline constexpr usize spawn_string_bytes     = 2048;
        inline constexpr u32 process_file_descriptors = 64;
        inline constexpr u32 iovec_segments           = 8;
        inline constexpr u32 pci_devices              = 256;
        inline constexpr u32 pci_resources            = 256;
        inline constexpr u32 registered_drivers       = 32;
        inline constexpr u32 bound_devices            = 64;
        inline constexpr u32 init_steps               = 128;
    }

    namespace queue {
        inline constexpr u32 aio_waiter_buckets    = 32;
        inline constexpr u32 aio_ring_slots        = abi::aio_ring_slots;
        inline constexpr u32 aio_ring_entries      = abi::aio_ring_entries;
        inline constexpr u32 aio_parked_operations = abi::aio_parked_operations;
        inline constexpr u32 scheduler_runnable    = 32;
        inline constexpr u32 resume                = 256;
        inline constexpr u32 signal_waiters        = 16;
        inline constexpr u32 unix_datagrams        = 32;
        inline constexpr u32 rcu_retired           = 256;
        inline constexpr u32 futex_waiters         = 64;
        inline constexpr u32 futex_buckets         = 4;
    }

    namespace coroutine {
        inline constexpr usize frame_slot_bytes = 2048;
        inline constexpr u32 frame_slots        = 32;
    }

} // namespace astia::resource_limits
