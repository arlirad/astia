#include <syscall.hpp>

namespace astia::net::syscalls {

    struct getsockopt_syscall
        : sys::syscall_handler<getsockopt_syscall, static_cast<sys::syscall_id>(73),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getsockopt_descriptor =
        getsockopt_syscall::descriptor;

} // namespace astia::net::syscalls
