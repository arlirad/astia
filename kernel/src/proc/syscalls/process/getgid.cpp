#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getgid
        : sys::syscall_handler<getgid, sys::syscall_id::getgid, sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* p = current_process();
            return p ? p->gid() : -ESRCH;
        }
    };

    extern const sys::syscall_descriptor getgid_descriptor = getgid::descriptor;

} // namespace astia::proc::syscalls
