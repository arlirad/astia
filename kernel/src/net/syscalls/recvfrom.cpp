#include <syscall.hpp>

namespace astia::net::syscalls {
    struct recvfrom : sys::syscall_handler<recvfrom, static_cast<sys::syscall_id>(76),
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::net::syscalls
