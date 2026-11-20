#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigtimedwait_syscall
        : sys::syscall_handler<sigtimedwait_syscall, sys::syscall_id::sigtimedwait,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigtimedwait_descriptor =
        sigtimedwait_syscall::descriptor;

} // namespace astia::proc::syscalls
