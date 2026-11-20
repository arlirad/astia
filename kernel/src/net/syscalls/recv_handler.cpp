#include <syscall.hpp>

namespace astia::net::syscalls {

    struct recv_syscall
        : sys::syscall_handler<recv_syscall, static_cast<sys::syscall_id>(75),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor recv_descriptor = recv_syscall::descriptor;

} // namespace astia::net::syscalls
