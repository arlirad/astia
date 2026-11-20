#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct msync_syscall : sys::syscall_handler<msync_syscall, sys::syscall_id::msync,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor msync_descriptor = msync_syscall::descriptor;

} // namespace astia::proc::syscalls
