#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigprocmask_syscall
        : sys::syscall_handler<sigprocmask_syscall, sys::syscall_id::sigprocmask,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigprocmask_descriptor =
        sigprocmask_syscall::descriptor;

} // namespace astia::proc::syscalls
