#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// getppid - Return the parent process identifier.
///
/// Synopsis:
///     long getppid(void);
///
/// Return Value:
///     Returns the parent process identifier or a negative errno.
namespace astia::proc::syscall {
    i64 getppid();
}

namespace astia::proc::syscall_handlers {

    struct getppid_syscall
        : sys::syscall_handler<getppid_syscall, sys::syscall_id::getppid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
