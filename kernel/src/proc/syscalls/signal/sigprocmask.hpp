#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// sigprocmask - block, unblock, or replace the signal mask.
    ///
    /// Synopsis:
    ///     long sigprocmask(int how, uint32_t set, uint32_t *old);
    ///
    /// Errors:
    ///     -EFAULT: The old mask pointer is inaccessible.
    ///     -EINVAL: The mask operation is unknown.
    ///     -ESRCH: There is no current process.
    i64 sigprocmask(i32 how, u32 set, u64 old_ptr);
}
