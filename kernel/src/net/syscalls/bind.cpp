#include <syscall.hpp>

namespace astia::net::syscalls {
    struct bind : sys::syscall_handler<bind, static_cast<sys::syscall_id>(71),
                                       sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor bind_descriptor = bind::descriptor;

} // namespace astia::net::syscalls
