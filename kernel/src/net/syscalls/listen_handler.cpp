#include <syscall.hpp>

namespace astia::net::syscalls {

    struct listen_syscall
        : sys::syscall_handler<listen_syscall, static_cast<sys::syscall_id>(74),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor listen_descriptor = listen_syscall::descriptor;

} // namespace astia::net::syscalls
