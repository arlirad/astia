#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// io_submit - Submit published asynchronous I/O operations.
///
/// Synopsis:
///     long io_submit(astia_ring *ring, uint32_t count);
///
/// Return Value:
///     Returns zero or a negative errno.
namespace astia::fs::syscall {
    i64 io_submit(u64 ring, u32 submit);
}

namespace astia::fs::syscall_handlers {

    struct io_submit_syscall
        : sys::syscall_handler<io_submit_syscall, sys::syscall_id::io_submit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::fs::syscall_handlers
