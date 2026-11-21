#include <syscall.hpp>

namespace astia::net::syscalls {
    struct socket : sys::syscall_handler<socket, static_cast<sys::syscall_id>(80),
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor socket_descriptor = socket::descriptor;

} // namespace astia::net::syscalls
