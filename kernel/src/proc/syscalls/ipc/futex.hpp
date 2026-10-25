#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// futex - Wait on or wake a shared user word.
    ///
    /// Synopsis:
    ///     long futex(uint32_t *address, uint32_t operation, uint32_t value, const timespec *timeout);
    ///
    /// Description:
    ///     FUTEX_WAIT blocks while the word equals value. FUTEX_WAKE wakes up
    ///     up to value waiters. The private flag is accepted for ABI shape.
    ///
    /// Return Value:
    ///     Wait returns zero; wake returns the number of woken waiters.
    ///
    /// Errors:
    ///     -EAGAIN: The word no longer equals the expected value.
    ///     -EFAULT: The futex word is not accessible userspace memory.
    ///     -EINVAL: The operation is unknown.
    ///     -ENOSYS: A timeout was requested but timed waits are not implemented.
    i64 futex(u64 address, u32 op, u32 expected, u64 timeout);
}
