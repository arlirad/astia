#include <syscall.hpp>

namespace astia::net::syscalls {
    struct listen : sys::syscall_handler<listen, static_cast<sys::syscall_id>(74),
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke([[maybe_unused]] sys::syscall_context& context) {
            co_return -ENOSYS;
        }
    };

    extern const sys::syscall_descriptor listen_descriptor = listen::descriptor;

} // namespace astia::net::syscalls
