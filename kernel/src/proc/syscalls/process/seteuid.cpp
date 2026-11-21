#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct seteuid : sys::syscall_handler<seteuid, sys::syscall_id::seteuid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::seteuid>(context);
        }
    };

    extern const sys::syscall_descriptor seteuid_descriptor = seteuid::descriptor;

} // namespace astia::proc::syscalls
