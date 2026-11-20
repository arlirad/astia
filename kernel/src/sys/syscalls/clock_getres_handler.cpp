#include <syscall.hpp>

namespace astia::sys::syscalls {

    struct clock_getres_syscall
        : sys::syscall_handler<clock_getres_syscall, sys::syscall_id::clock_getres,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor clock_getres_descriptor =
        clock_getres_syscall::descriptor;

} // namespace astia::sys::syscalls
