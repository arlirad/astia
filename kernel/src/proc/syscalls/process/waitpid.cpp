#include "waitpid.hpp"

#include <astia/api/errno.h>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
namespace astia::proc::syscall {
    i64 waitpid(i32 pid, u64 status_ptr, u32 options) {
        // Keep the POSIX selector grammar at the ABI edge, but validate each
        // identifier through its domain mapper before the scheduler-facing
        // implementation sees the legacy signed selector.
        if (pid > 0 && !mapper<pid_t>::from(pid))
            return -EINVAL;
        if (pid < -1 && !mapper<pgrp_t>::from(-static_cast<i64>(pid)))
            return -EINVAL;
        const sys::user_ptr<i32> status(status_ptr);
        const auto               validated_status =
            status.null() ? optional<sys::validated_user_ptr<i32>>{} : status.validate();
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
}

namespace astia::proc::syscalls {

    /// ABI ID: waitpid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct waitpid : sys::syscall_handler<waitpid, sys::syscall_id::waitpid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor waitpid_descriptor = waitpid::descriptor;

} // namespace astia::proc::syscalls
