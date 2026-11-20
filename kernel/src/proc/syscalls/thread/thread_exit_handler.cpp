#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct thread_exit_syscall
        : sys::syscall_handler<thread_exit_syscall, sys::syscall_id::thread_exit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_exit_descriptor =
        thread_exit_syscall::descriptor;

} // namespace astia::proc::syscalls
