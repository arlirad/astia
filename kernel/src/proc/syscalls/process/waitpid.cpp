#include <astia/api/errno.h>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {

    /// waitpid - wait for and reap a child process.
    ///
    /// Synopsis:
    ///     long waitpid(int pid, int *status, uint32_t options);
    ///
    /// Description:
    ///     pid > 0 selects one child. pid == -1 selects any child.
    ///     WNOHANG returns immediately when no child has exited.
    ///
    /// Return Value:
    ///     Returns the reaped child PID, or zero with WNOHANG when no child is ready.
    ///
    /// Errors:
    ///     -EFAULT: The status pointer is not writable userspace memory.
    ///     -ECHILD: The caller has no matching child process.
    ///     -EINVAL: The PID or option mask is invalid.
    ///     -ESRCH: There is no current process or thread.
    struct waitpid : sys::syscall_handler<waitpid, sys::syscall_id::waitpid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(i32 pid, sys::user_ptr<i32> status, u32 options) {
            // Keep the POSIX selector grammar at the ABI edge, but validate each
            // identifier through its domain mapper before the scheduler-facing
            // implementation sees the legacy signed selector.
            if (pid > 0 && !mapper<pid_t>::from(pid))
                return -EINVAL;
            if (pid < -1 && !mapper<pgrp_t>::from(-static_cast<i64>(pid)))
                return -EINVAL;
            const auto validated_status = status.null()
                                              ? optional<sys::validated_user_ptr<i32>>{}
                                              : status.validate();
            if (!status.null() && !validated_status)
                return -EFAULT;
            i32  local  = 0;
            auto mapped = mapper<wait_options>::from(options);
            if (!mapped)
                return -EINVAL;
            const i64 result =
                proc::waitpid(pid, status.null() ? nullptr : &local, mapped.value);
            if (result >= 0 && !status.null() &&
                !sys::copy_out(validated_status.value.bytes(), &local))
                return -EFAULT;
            return result;
        }
    };

    extern const sys::syscall_descriptor waitpid_descriptor = waitpid::descriptor;

} // namespace astia::proc::syscalls
