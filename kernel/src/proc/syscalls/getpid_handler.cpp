#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getpid - return the calling process ID.
    ///
    /// Synopsis:
    ///     long getpid(void);
    ///
    /// Return Value:
    ///     The calling process ID.
    struct getpid_syscall : sys::syscall_handler<getpid_syscall, sys::syscall_id::getpid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpid_descriptor = getpid_syscall::descriptor;

} // namespace astia::proc::syscalls
