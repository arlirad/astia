#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_join - Wait for and reap a user thread.
    ///
    /// Synopsis:
    ///     long thread_join(uint32_t tid, void **result);
    ///
    /// Description:
    ///     Waits for tid to terminate and optionally copies its result pointer
    ///     to result. Only one joiner is allowed.
    ///
    /// Return Value:
    ///     Returns zero after reaping the thread or a negative errno.
    ///
    /// Errors:
    ///     -EFAULT: The result pointer is not writable userspace memory.
    ///     -EINVAL: The thread is detached, already joined, or is the caller.
    ///     -ESRCH: The thread does not exist.
    i64 thread_join(u32 tid, u64 result_ptr);
}
