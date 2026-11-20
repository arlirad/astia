#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

namespace astia::proc::syscall {

    /// kill - send a signal to a process or process group
    ///
    /// Synopsis:
    ///     long kill(int pid, int signal);
    ///
    /// Description:
    ///     pid > 0 targets one process, pid == 0 targets the caller's
    ///     process group, and pid < 0 targets process group -pid. Signal
    ///     zero performs an existence check without enqueuing a signal.
    ///
    /// Return Value:
    ///     Returns zero on success.
    ///
    /// Errors:
    ///     -EINVAL: signal or target identifier is malformed.
    ///     -ESRCH: no matching live process or process group exists.
    i64 kill(i32 pid_raw, i32 signal_raw);

} // namespace astia::proc::syscall

namespace astia::proc::syscall_handlers {

    struct kill_syscall : sys::syscall_handler<kill_syscall, sys::syscall_id::kill,
                                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
