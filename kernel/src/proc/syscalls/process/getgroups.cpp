#include "credentials.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getgroups : sys::syscall_handler<getgroups, sys::syscall_id::getgroups,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::getgroups>(context);
        }
    };

    extern const sys::syscall_descriptor getgroups_descriptor = getgroups::descriptor;

} // namespace astia::proc::syscalls
