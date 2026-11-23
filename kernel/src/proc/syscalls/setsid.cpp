#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// setsid - create a new session for the caller.
    ///
    /// Synopsis:
    ///     long setsid(void);
    ///
    /// Errors:
    ///     -EPERM: The caller is already a process-group leader.
    ///     -ESRCH: There is no current process.
    struct setsid
        : sys::syscall_handler<setsid, sys::syscall_id::setsid, sys::syscall_mode::sync> {
        static i64 invoke() {
            return proc::setsid();
        }
    };

} // namespace astia::proc::syscalls
