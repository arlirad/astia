#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getpgid - Return a process group identifier.
    ///
    /// Synopsis:
    ///     long getpgid(int pid);
    ///
    /// Return Value:
    ///     Returns the process group identifier or a negative errno.
    struct getpgid_syscall
        : sys::syscall_handler<getpgid_syscall, sys::syscall_id::getpgid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpgid_descriptor = getpgid_syscall::descriptor;

} // namespace astia::proc::syscalls
