#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct geteuid_syscall
        : sys::syscall_handler<geteuid_syscall, sys::syscall_id::geteuid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor geteuid_descriptor = geteuid_syscall::descriptor;

} // namespace astia::proc::syscalls
