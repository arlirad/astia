#include <syscall.hpp>

namespace astia::net::syscalls {
    struct accept : sys::syscall_handler<accept, static_cast<sys::syscall_id>(70),
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor accept_descriptor = accept::descriptor;

} // namespace astia::net::syscalls
