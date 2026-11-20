#include <syscall.hpp>

namespace astia::net::syscalls {

    struct accept_syscall
        : sys::syscall_handler<accept_syscall, static_cast<sys::syscall_id>(70),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor accept_descriptor = accept_syscall::descriptor;

} // namespace astia::net::syscalls
