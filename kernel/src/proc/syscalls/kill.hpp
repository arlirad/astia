#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {

    /// kill — send a signal to a process or process group
    ///
    /// Synopsis:
    ///     long kill(int pid, int signal);
    ///
    /// Description:
    ///     pid > 0 targets one process, pid == 0 targets the caller's
    ///     process group, and pid < 0 targets process group -pid. Signal
    ///     zero performs an existence check without enqueuing a signal.
    ///
    /// Errors: -EINVAL for malformed identifiers/signals; -ESRCH when no
    /// matching live process or process group exists.
    i64 kill(i32 pid_raw, i32 signal_raw);

} // namespace astia::proc::syscall
