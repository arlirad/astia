#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getgid
        : sys::syscall_handler<getgid, sys::syscall_id::getgid, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::getgid>(context);
        }
    };

    extern const sys::syscall_descriptor getgid_descriptor = getgid::descriptor;

} // namespace astia::proc::syscalls
