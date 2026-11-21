#include <syscall.hpp>

namespace astia::net::syscalls {
    struct recv : sys::syscall_handler<recv, static_cast<sys::syscall_id>(75),
                                       sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor recv_descriptor = recv::descriptor;

} // namespace astia::net::syscalls
