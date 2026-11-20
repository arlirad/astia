#pragma once

#include <syscall.hpp>

namespace astia::fs::syscall_handlers {

    /// shm_unlink - Dispatch the shm unlink syscall.
    ///
    /// Synopsis:
    ///     long shm_unlink(...);
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
    struct shm_unlink_syscall
        : sys::syscall_handler<shm_unlink_syscall, sys::syscall_id::shm_unlink,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::fs::syscall_handlers
