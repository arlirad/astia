#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// sigaction - get or change a signal disposition.
    ///
    /// Synopsis:
    ///     long sigaction(int signal, const action *next, action *old);
    ///
    /// Errors:
    ///     -EFAULT: An action pointer or signal restorer is inaccessible.
    ///     -EINVAL: The signal is invalid or cannot be caught.
    ///     -ESRCH: There is no current process.
    i64 sigaction(i32 value, u64 next_ptr, u64 old_ptr);
}
