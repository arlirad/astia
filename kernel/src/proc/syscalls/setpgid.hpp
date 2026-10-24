#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// setpgid - move a process into a process group.
    ///
    /// Synopsis:
    ///     long setpgid(int pid, int pgid);
    ///
    /// Errors:
    ///     -EINVAL: pid or pgid is invalid after special-value handling.
    ///     -EPERM: The target is not the caller's child.
    ///     -EPERM: The target belongs to another session.
    ///     -EPERM: The target is a session leader.
    ///     -ESRCH: The target process does not exist.
    i64 setpgid(i32 pid_raw, i32 pgid_raw);
}
