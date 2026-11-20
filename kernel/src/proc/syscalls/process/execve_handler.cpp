#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct execve_syscall : sys::syscall_handler<execve_syscall, sys::syscall_id::execve,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor execve_descriptor = execve_syscall::descriptor;

} // namespace astia::proc::syscalls
