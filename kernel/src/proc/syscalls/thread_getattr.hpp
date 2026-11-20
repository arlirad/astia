#pragma once

#include <syscall.hpp>

namespace astia::proc::syscall_handlers {

    /// thread_getattr - Dispatch the thread getattr syscall.
    ///
    /// Synopsis:
    ///     long thread_getattr(...);
    ///
    /// Description:
    ///     Validates and dispatches the request through the owning subsystem.
    ///
    /// Return Value:
    ///     Returns the subsystem result or a negative errno.
    ///
    /// Errors:
    ///     -ENOSYS: The operation is unavailable in the current kernel configuration.
    ///
    /// Notes:
    ///     This handler preserves the public ABI number and is asynchronous at the
    ///     kernel boundary; completion is owned by the syscall dispatcher.
    struct thread_getattr_syscall
        : sys::syscall_handler<thread_getattr_syscall, sys::syscall_id::thread_getattr,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
