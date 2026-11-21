#include <syscall.hpp>

namespace astia::net::syscalls {
    struct connect : sys::syscall_handler<connect, static_cast<sys::syscall_id>(72),
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor connect_descriptor = connect::descriptor;

} // namespace astia::net::syscalls
