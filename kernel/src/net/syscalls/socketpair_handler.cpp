#include <syscall.hpp>

namespace astia::net::syscalls {

    struct socketpair_syscall
        : sys::syscall_handler<socketpair_syscall, static_cast<sys::syscall_id>(81),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor socketpair_descriptor =
        socketpair_syscall::descriptor;

} // namespace astia::net::syscalls
