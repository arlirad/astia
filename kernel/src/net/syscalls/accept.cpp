#include <syscall.hpp>

namespace astia::net::syscalls {
    struct accept : sys::syscall_handler<accept, static_cast<sys::syscall_id>(70),
                                         sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor accept_descriptor = accept::descriptor;

} // namespace astia::net::syscalls
