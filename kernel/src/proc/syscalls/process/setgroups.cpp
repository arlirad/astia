#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setgroups : sys::syscall_handler<setgroups, sys::syscall_id::setgroups,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::setgroups>(context);
        }
    };

    extern const sys::syscall_descriptor setgroups_descriptor = setgroups::descriptor;

} // namespace astia::proc::syscalls
