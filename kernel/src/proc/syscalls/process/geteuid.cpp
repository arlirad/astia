#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct geteuid : sys::syscall_handler<geteuid, sys::syscall_id::geteuid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::geteuid>(context);
        }
    };

    extern const sys::syscall_descriptor geteuid_descriptor = geteuid::descriptor;

} // namespace astia::proc::syscalls
