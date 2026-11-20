#include <syscall.hpp>

namespace astia::net::syscalls {

    struct sendto_syscall
        : sys::syscall_handler<sendto_syscall, static_cast<sys::syscall_id>(78),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sendto_descriptor = sendto_syscall::descriptor;

} // namespace astia::net::syscalls
