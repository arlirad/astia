#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigaltstack_syscall
        : sys::syscall_handler<sigaltstack_syscall, sys::syscall_id::sigaltstack,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigaltstack_descriptor =
        sigaltstack_syscall::descriptor;

} // namespace astia::proc::syscalls
