#include <syscall.hpp>

namespace astia::net::syscalls {
    struct connect : sys::syscall_handler<connect, static_cast<sys::syscall_id>(72),
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor connect_descriptor = connect::descriptor;

} // namespace astia::net::syscalls
