#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigsuspend_syscall
        : sys::syscall_handler<sigsuspend_syscall, sys::syscall_id::sigsuspend,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigsuspend_descriptor =
        sigsuspend_syscall::descriptor;

} // namespace astia::proc::syscalls
