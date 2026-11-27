#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getpgid : sys::syscall_handler<getpgid, sys::syscall_id::getpgid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(i64 raw_target) {
            if (raw_target < 0 || raw_target > 0xffffffffll)
                return -ESRCH;

            return proc::getpgid(pid_from_raw(static_cast<u32>(raw_target)));
        }
    };
} // namespace astia::proc::syscalls
