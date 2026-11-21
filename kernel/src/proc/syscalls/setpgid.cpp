#include <astia/api/errno.h>

#include "../mapping.hpp"
#include "../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 setpgid(i32 pid_raw, i32 pgid_raw) {
        pid_t pid{};
        if (pid_raw != 0) {
            auto mapped = mapper<pid_t>::from(pid_raw);
            if (!mapped)
                return -EINVAL;
            pid = mapped.value;
        }
        pgrp_t group{};
        if (pgid_raw != 0) {
            auto mapped = mapper<pgrp_t>::from(pgid_raw);
            if (!mapped)
                return -EINVAL;
            group = mapped.value;
        }
        return proc::setpgid(pid, group);
    }
}

namespace astia::proc::syscalls {

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
    struct setpgid : sys::syscall_handler<setpgid, sys::syscall_id::setpgid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::setpgid>(context);
        }
    };

    extern const sys::syscall_descriptor setpgid_descriptor = setpgid::descriptor;

} // namespace astia::proc::syscalls
