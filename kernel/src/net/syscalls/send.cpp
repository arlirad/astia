#include <syscall.hpp>

namespace astia::net::syscalls {
    struct send : sys::syscall_handler<send, static_cast<sys::syscall_id>(77),
                                       sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor send_descriptor = send::descriptor;

} // namespace astia::net::syscalls
