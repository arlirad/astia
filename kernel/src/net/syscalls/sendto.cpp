#include <syscall.hpp>

namespace astia::net::syscalls {
    struct sendto : sys::syscall_handler<sendto, static_cast<sys::syscall_id>(78),
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor sendto_descriptor = sendto::descriptor;

} // namespace astia::net::syscalls
