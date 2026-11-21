#include "rlimit.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getrlimit : sys::syscall_handler<getrlimit, sys::syscall_id::getrlimit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::getrlimit>(context);
        }
    };

    extern const sys::syscall_descriptor getrlimit_descriptor = getrlimit::descriptor;

} // namespace astia::proc::syscalls
