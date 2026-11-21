#include <syscall.hpp>

namespace astia::net::syscalls {
    struct send : sys::syscall_handler<send, static_cast<sys::syscall_id>(77),
                                       sys::syscall_mode::sync> {
        static i64 invoke([[maybe_unused]] sys::syscall_context& context) {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor send_descriptor = send::descriptor;

} // namespace astia::net::syscalls
