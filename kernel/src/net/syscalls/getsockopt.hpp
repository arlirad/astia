#pragma once

#include <syscall.hpp>

namespace astia::net::syscall_handlers {

    /// getsockopt - Dispatch the getsockopt syscall.
    ///
    /// Synopsis:
    ///     long getsockopt(...);
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
    struct getsockopt_syscall
        : sys::syscall_handler<getsockopt_syscall, static_cast<sys::syscall_id>(73),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::net::syscall_handlers
