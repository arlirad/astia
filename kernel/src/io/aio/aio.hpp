#pragma once

#include <astia/api/aio.h>
#include <astia/common.hpp>
#include <astia/fs.hpp>
#include <astia/io/aio.hpp>
#include <astia/mm/iovec.hpp>
#include <astia/resource_limits.hpp>

namespace astia::proc {
    class process;
}

namespace astia::io::aio {

    inline constexpr u32 kernel_ring_max_entries = ASTIA_AIO_MAX_RING_ENTRIES;
    static_assert(ASTIA_AIO_MAX_RING_SLOTS == resource_limits::abi::aio_ring_slots);
    static_assert(ASTIA_AIO_MAX_RING_ENTRIES == resource_limits::abi::aio_ring_entries);
    static_assert(ASTIA_AIO_MAX_PARKED_OPERATIONS ==
                  resource_limits::abi::aio_parked_operations);
    static_assert(ASTIA_AIO_MAX_OPERATION_BYTES ==
                  resource_limits::abi::aio_operation_bytes);
    static_assert(kernel_ring_max_entries <= IO_MAX_ENTRIES,
                  "kernel ring cap must fit the AIO ABI cap");

    struct user_memory_adapter {
        bool (*user_span_ok)(proc::process*, span<u8>);
        bool (*user_span_writable_ok)(proc::process*, span<u8>);
        bool (*kernel_span_ok)(span<u8>);
        long (*pin_user_span)(proc::process*, span<u8>, ::astia::io::direction,
                              mm::iovec&, bool);
        bool (*copy_in)(proc::process*, void*, span<u8>);
        bool (*copy_in_string)(proc::process*, span<char>, span<u8>);
        bool (*copy_out)(proc::process*, span<u8>, const void*);
        bool (*realtime_now)(u64&, u64&);
    };

    void set_user_memory_adapter(const user_memory_adapter& adapter);
    const user_memory_adapter& user_memory();

    long io_setup(u32 entries, u32 flags, astia_rings** out);
    long io_destroy(astia_rings* ring);
    long io_submit(astia_rings* ring, u32 to_submit);
    bool io_allow_kernel_buffers(astia_rings* ring);
    void io_device_ready(i32 fd);
    void io_device_ready_context(const io_device* dev);
    void io_device_ready_context_except(const io_device* dev, u64 excluded_user_data);
    void io_process_deferred_ready();
    void io_interrupt_process(proc::process* owner, i32 result);
    void io_poll_tick();
    void io_exec_process(proc::process* owner);
    void io_exit_process(proc::process* owner);

} // namespace astia::io::aio
