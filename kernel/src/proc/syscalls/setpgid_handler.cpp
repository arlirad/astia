#include <syscall.hpp>

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
    struct setpgid_syscall
        : sys::syscall_handler<setpgid_syscall, sys::syscall_id::setpgid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setpgid_descriptor = setpgid_syscall::descriptor;

} // namespace astia::proc::syscalls
