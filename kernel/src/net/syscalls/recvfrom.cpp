#include <syscall.hpp>

namespace astia::net::syscalls {
    struct recvfrom : sys::syscall_handler<recvfrom, static_cast<sys::syscall_id>(76),
                                           sys::syscall_mode::sync> {
        static i64 invoke([[maybe_unused]] sys::syscall_context& context) {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor recvfrom_descriptor = recvfrom::descriptor;

} // namespace astia::net::syscalls
