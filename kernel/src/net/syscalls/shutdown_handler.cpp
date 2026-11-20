#include <syscall.hpp>

namespace astia::net::syscalls {

    struct shutdown_syscall
        : sys::syscall_handler<shutdown_syscall, static_cast<sys::syscall_id>(79),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor shutdown_descriptor =
        shutdown_syscall::descriptor;

} // namespace astia::net::syscalls
