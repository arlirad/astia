#include "../time.hpp"

#include <syscall.hpp>

namespace astia::sys::syscalls {
    struct clock_gettime
        : sys::syscall_handler<clock_gettime, sys::syscall_id::clock_gettime,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::sys::sys_clock_gettime>(context);
        }
    };

    extern const sys::syscall_descriptor clock_gettime_descriptor =
        clock_gettime::descriptor;

} // namespace astia::sys::syscalls
