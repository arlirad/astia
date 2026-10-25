#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_exit - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 thread_exit(i64 result);
}
