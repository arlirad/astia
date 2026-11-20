#include <syscall.hpp>

namespace astia::net::syscalls {

    struct socket_syscall
        : sys::syscall_handler<socket_syscall, static_cast<sys::syscall_id>(80),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor socket_descriptor = socket_syscall::descriptor;

} // namespace astia::net::syscalls
