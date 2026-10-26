#include "waitpid.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
namespace astia::proc::syscall {
    i64 waitpid(i32 pid, u64 status_ptr, u32 options) {
        // Keep the POSIX selector grammar at the ABI edge, but validate each
        // identifier through its domain mapper before the scheduler-facing
        // implementation sees the legacy signed selector.
        if (pid > 0 && !mapper<pid_t>::from(pid)) return -EINVAL;
        if (pid < -1 && !mapper<pgrp_t>::from(-static_cast<i64>(pid))) return -EINVAL;
        const sys::user_range status = sys::user_range::bytes(status_ptr, sizeof(i32));
        if (!status.null() && !sys::user_range_ok(status)) return -EFAULT;
        i32 local = 0;
        auto mapped = mapper<wait_options>::from(options);
        if (!mapped) return -EINVAL;
        const i64 result = proc::waitpid(pid, status.null() ? nullptr : &local, mapped.value);
        if (result >= 0 && !status.null() && !sys::copy_out(status, &local)) return -EFAULT;
        return result;
    }
}
