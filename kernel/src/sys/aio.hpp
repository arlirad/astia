#pragma once

#include <astia/io/aio.hpp>
#include <astia/resource_limits.hpp>

namespace astia::sys {

    using io_device = ::astia::io::aio::io_device;

    inline constexpr u32 kernel_ring_max_entries = ASTIA_AIO_MAX_RING_ENTRIES;

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

} // namespace astia::sys
