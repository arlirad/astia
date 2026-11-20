#include <syscall.hpp>

namespace astia::net::syscalls {

    struct send_syscall
        : sys::syscall_handler<send_syscall, static_cast<sys::syscall_id>(77),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor send_descriptor = send_syscall::descriptor;

} // namespace astia::net::syscalls
