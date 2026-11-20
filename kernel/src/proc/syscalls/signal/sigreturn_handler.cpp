#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigreturn_syscall
        : sys::syscall_handler<sigreturn_syscall, sys::syscall_id::sigreturn,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigreturn_descriptor =
        sigreturn_syscall::descriptor;

} // namespace astia::proc::syscalls
