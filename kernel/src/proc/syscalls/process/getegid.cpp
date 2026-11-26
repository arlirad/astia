#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getegid : sys::syscall_handler<getegid, sys::syscall_id::getegid,
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* p = current_process();

            return p ? p->egid() : -ESRCH;
        }
    };
} // namespace astia::proc::syscalls
