#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigwait_syscall
        : sys::syscall_handler<sigwait_syscall, sys::syscall_id::sigwait,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigwait_descriptor = sigwait_syscall::descriptor;

} // namespace astia::proc::syscalls
