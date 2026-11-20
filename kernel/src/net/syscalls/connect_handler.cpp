#include <syscall.hpp>

namespace astia::net::syscalls {

    struct connect_syscall
        : sys::syscall_handler<connect_syscall, static_cast<sys::syscall_id>(72),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor connect_descriptor = connect_syscall::descriptor;

} // namespace astia::net::syscalls
