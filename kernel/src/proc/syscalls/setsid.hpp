#pragma once
#include <astia/common.hpp>
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
