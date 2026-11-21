#include "../time.hpp"

#include <syscall.hpp>

namespace astia::sys::syscalls {
    struct clock_getres
        : sys::syscall_handler<clock_getres, sys::syscall_id::clock_getres,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::sys::sys_clock_getres>(context);
        }
    };

    extern const sys::syscall_descriptor clock_getres_descriptor =
        clock_getres::descriptor;

} // namespace astia::sys::syscalls
