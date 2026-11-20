#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// getpgid - Return a process group identifier.
///
/// Synopsis:
///     long getpgid(int pid);
///
/// Return Value:
///     Returns the process group identifier or a negative errno.
namespace astia::proc::syscall {
    i64 getpgid(i64 target);
}

namespace astia::proc::syscall_handlers {

    struct getpgid_syscall
        : sys::syscall_handler<getpgid_syscall, sys::syscall_id::getpgid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
