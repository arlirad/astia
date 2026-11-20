#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct thread_create_syscall
        : sys::syscall_handler<thread_create_syscall, sys::syscall_id::thread_create,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_create_descriptor =
        thread_create_syscall::descriptor;

} // namespace astia::proc::syscalls
