#include <syscall.hpp>

namespace astia::net::syscalls {
    struct sendto : sys::syscall_handler<sendto, static_cast<sys::syscall_id>(78),
                                         sys::syscall_mode::sync> {
        static i64 invoke([[maybe_unused]] sys::syscall_context& context) {
            return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor sendto_descriptor = sendto::descriptor;

} // namespace astia::net::syscalls
