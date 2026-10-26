#include "setpgid.hpp"
#include "../mapping.hpp"
#include "../proc.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 setpgid(i32 pid_raw, i32 pgid_raw) {
        pid_t pid{};
        if (pid_raw != 0) {
            auto mapped = mapper<pid_t>::from(pid_raw);
            if (!mapped) return -EINVAL;
            pid = mapped.value;
        }
        pgrp_t group{};
        if (pgid_raw != 0) {
            auto mapped = mapper<pgrp_t>::from(pgid_raw);
            if (!mapped) return -EINVAL;
            group = mapped.value;
        }
        return proc::setpgid(pid, group);
    }
}
