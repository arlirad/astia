#include <syscall.hpp>

namespace astia::net::syscalls {

    struct bind_syscall
        : sys::syscall_handler<bind_syscall, static_cast<sys::syscall_id>(71),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor bind_descriptor = bind_syscall::descriptor;

} // namespace astia::net::syscalls
