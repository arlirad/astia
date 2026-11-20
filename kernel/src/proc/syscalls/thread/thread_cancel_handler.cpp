#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct thread_cancel_syscall
        : sys::syscall_handler<thread_cancel_syscall, sys::syscall_id::thread_cancel,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_cancel_descriptor =
        thread_cancel_syscall::descriptor;

} // namespace astia::proc::syscalls
