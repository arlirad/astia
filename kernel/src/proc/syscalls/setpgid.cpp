#include <astia/api/errno.h>

#include "../mapping.hpp"
#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct setpgid : sys::syscall_handler<setpgid, sys::syscall_id::setpgid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(i32 pid_raw, i32 pgid_raw) {
            pid_t pid{};

            if (pid_raw != 0) {
                auto mapped = mapper<pid_t>::from(pid_raw);
                if (!mapped)
                    return -EINVAL;

                pid = mapped.value;
            }

            pgrp_t group{};

            if (pgid_raw != 0) {
                auto mapped = mapper<pgrp_t>::from(pgid_raw);
                if (!mapped)
                    return -EINVAL;

                group = mapped.value;
            }

            return proc::setpgid(pid, group);
        }
    };
} // namespace astia::proc::syscalls
