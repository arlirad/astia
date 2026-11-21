#include <syscall.hpp>

namespace astia::net::syscalls {
    struct getsockopt : sys::syscall_handler<getsockopt, static_cast<sys::syscall_id>(73),
                                             sys::syscall_mode::sync> {
        static i64 invoke([[maybe_unused]] sys::syscall_context& context) {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::net::syscalls
