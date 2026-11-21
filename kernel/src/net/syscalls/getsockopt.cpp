#include <syscall.hpp>

namespace astia::net::syscalls {
    struct getsockopt : sys::syscall_handler<getsockopt, static_cast<sys::syscall_id>(73),
                                             sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor getsockopt_descriptor = getsockopt::descriptor;

} // namespace astia::net::syscalls
