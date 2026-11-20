#pragma once

#include <syscall.hpp>

namespace astia::sys::syscall_handlers {

    /// clock_getres - Dispatch the clock getres syscall.
    ///
    /// Synopsis:
    ///     long clock_getres(...);
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
    struct clock_getres_syscall
        : sys::syscall_handler<clock_getres_syscall, sys::syscall_id::clock_getres,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::sys::syscall_handlers
