#include <syscall.hpp>

namespace astia::net::syscalls {
    struct listen : sys::syscall_handler<listen, static_cast<sys::syscall_id>(74),
                                         sys::syscall_mode::sync> {
        static i64 invoke() {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::net::syscalls
