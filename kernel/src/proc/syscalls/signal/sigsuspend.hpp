#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// sigsuspend - wait for a signal with a temporary mask.
    ///
    /// Synopsis:
    ///     long sigsuspend(const uint32_t *mask);
    ///
    /// Errors:
    ///     -EFAULT: The mask pointer is inaccessible.
    ///     -EINTR: A signal interrupted the suspended thread.
    ///     -ESRCH: There is no current process or thread.
    i64 sigsuspend(u64 mask_ptr);
}
