#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setuid
        : sys::syscall_handler<setuid, sys::syscall_id::setuid, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::setuid>(context);
        }
    };

    extern const sys::syscall_descriptor setuid_descriptor = setuid::descriptor;

} // namespace astia::proc::syscalls
