#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setreuid : sys::syscall_handler<setreuid, sys::syscall_id::setreuid,
                                           sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::setreuid>(context);
        }
    };

    extern const sys::syscall_descriptor setreuid_descriptor = setreuid::descriptor;

} // namespace astia::proc::syscalls
