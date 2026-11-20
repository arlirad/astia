#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    /// getpid - return the calling process ID.
    ///
    /// Synopsis:
    ///     long getpid(void);
    ///
    /// Return Value:
    ///     The calling process ID.
    i64 getpid();
}

namespace astia::proc::syscall_handlers {

    struct getpid_syscall : sys::syscall_handler<getpid_syscall, sys::syscall_id::getpid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
