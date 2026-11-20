#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    /// setsid - create a new session for the caller.
    ///
    /// Synopsis:
    ///     long setsid(void);
    ///
    /// Errors:
    ///     -EPERM: The caller is already a process-group leader.
    ///     -ESRCH: There is no current process.
    i64 setsid();
}

namespace astia::proc::syscall_handlers {

    struct setsid_syscall : sys::syscall_handler<setsid_syscall, sys::syscall_id::setsid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
