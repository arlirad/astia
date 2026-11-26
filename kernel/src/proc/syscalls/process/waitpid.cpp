#include <astia/api/errno.h>
#include <astia/error.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {
    struct waitpid : sys::syscall_handler<waitpid, sys::syscall_id::waitpid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(i32 pid, sys::user_ptr<i32> status, u32 options) {
            // Keep the POSIX selector grammar at the ABI edge, but validate each
            // identifier through its domain mapper before the scheduler-facing
            // implementation sees the legacy signed selector.
            if (pid > 0 && !mapper<pid_t>::from(pid))
                co_return -EINVAL;

            if (pid < -1 && !mapper<pgrp_t>::from(-static_cast<i64>(pid)))
                co_return -EINVAL;

            const auto validated_status = status.null()
                                              ? optional<sys::validated_user_ptr<i32>>{}
                                              : status.validate();

            if (!status.null() && !validated_status)
                co_return -EFAULT;

            i32 local = 0;

            auto mapped = mapper<wait_options>::from(options);
            if (!mapped)
                co_return -EINVAL;

            auto waited = co_await proc::waitpid(pid, status.null() ? nullptr : &local,
                                                 mapped.value);
            if (!waited)
                co_return error_errno(waited.error);

            const i64 result = waited.value;

            if (result >= 0 && !status.null() &&
                !sys::copy_out(validated_status.value.bytes(), &local))
                co_return -EFAULT;

            co_return result;
        }
    };
} // namespace astia::proc::syscalls
