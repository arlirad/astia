#include <syscall.hpp>

namespace astia::net::syscalls {
    struct recv : sys::syscall_handler<recv, static_cast<sys::syscall_id>(75),
                                       sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor recv_descriptor = recv::descriptor;

} // namespace astia::net::syscalls
