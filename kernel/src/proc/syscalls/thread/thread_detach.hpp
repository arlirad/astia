#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_detach - Detach a user thread.
    ///
    /// Synopsis:
    ///     long thread_detach(uint32_t tid);
    ///
    /// Description:
    ///     Marks tid detached so its resources are reclaimed after exit and it
    ///     cannot later be joined.
    ///
    /// Return Value:
    ///     Returns zero or a negative errno.
    ///
    /// Errors:
    ///     -EINVAL: The thread already has a joiner.
    ///     -ESRCH: The thread does not exist.
    i64 thread_detach(u32 tid);
}
