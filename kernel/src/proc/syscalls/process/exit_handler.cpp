#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct exit_syscall : sys::syscall_handler<exit_syscall, sys::syscall_id::exit,
                                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor exit_descriptor = exit_syscall::descriptor;

} // namespace astia::proc::syscalls
