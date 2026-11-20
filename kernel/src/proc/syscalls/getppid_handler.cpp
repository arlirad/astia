#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getppid - Return the parent process identifier.
    ///
    /// Synopsis:
    ///     long getppid(void);
    ///
    /// Return Value:
    ///     Returns the parent process identifier or a negative errno.
    struct getppid_syscall
        : sys::syscall_handler<getppid_syscall, sys::syscall_id::getppid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getppid_descriptor = getppid_syscall::descriptor;

} // namespace astia::proc::syscalls
