#pragma once

#include <syscall.hpp>

namespace astia::sys::syscall_handlers {

    /// clock_gettime - Dispatch the clock gettime syscall.
    ///
    /// Synopsis:
    ///     long clock_gettime(...);
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
    struct clock_gettime_syscall
        : sys::syscall_handler<clock_gettime_syscall, sys::syscall_id::clock_gettime,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::sys::syscall_handlers
