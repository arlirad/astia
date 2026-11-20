#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct fork_syscall : sys::syscall_handler<fork_syscall, sys::syscall_id::fork,
                                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor fork_descriptor = fork_syscall::descriptor;

} // namespace astia::proc::syscalls
