#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getuid
        : sys::syscall_handler<getuid, sys::syscall_id::getuid, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::getuid>(context);
        }
    };

    extern const sys::syscall_descriptor getuid_descriptor = getuid::descriptor;

} // namespace astia::proc::syscalls
