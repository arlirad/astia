#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// waitpid - wait for and reap a child process.
    ///
    /// Synopsis:
    ///     long waitpid(int pid, int *status, uint32_t options);
    ///
    /// Description:
    ///     pid > 0 selects one child. pid == -1 selects any child.
    ///     WNOHANG returns immediately when no child has exited.
    ///
    /// Return Value:
    ///     Returns the reaped child PID, or zero with WNOHANG when no child is ready.
    ///
    /// Errors:
    ///     -EFAULT: The status pointer is not writable userspace memory.
    ///     -ECHILD: The caller has no matching child process.
    ///     -EINVAL: The PID or option mask is invalid.
    ///     -ESRCH: There is no current process or thread.
    i64 waitpid(i32 pid, u64 status_ptr, u32 options);
}
