#include <syscall.hpp>

namespace astia::net::syscalls {
    struct socketpair : sys::syscall_handler<socketpair, static_cast<sys::syscall_id>(81),
                                             sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor socketpair_descriptor = socketpair::descriptor;

} // namespace astia::net::syscalls
