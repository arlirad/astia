#include <syscall.hpp>

namespace astia::net::syscalls {
    struct shutdown : sys::syscall_handler<shutdown, static_cast<sys::syscall_id>(79),
                                           sys::syscall_mode::sync> {
        static i64 invoke([[maybe_unused]] sys::syscall_context& context) {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor shutdown_descriptor = shutdown::descriptor;

} // namespace astia::net::syscalls
