#include <syscall.hpp>

namespace astia::net::syscalls {

    struct recvfrom_syscall
        : sys::syscall_handler<recvfrom_syscall, static_cast<sys::syscall_id>(76),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor recvfrom_descriptor =
        recvfrom_syscall::descriptor;

} // namespace astia::net::syscalls
