#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getuid
        : sys::syscall_handler<getuid, sys::syscall_id::getuid, sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* p = current_process();
            return p ? p->uid() : -ESRCH;
        }
    };

    extern const sys::syscall_descriptor getuid_descriptor = getuid::descriptor;

} // namespace astia::proc::syscalls
