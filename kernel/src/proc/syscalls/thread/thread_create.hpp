#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_create - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 thread_create(u64 entry, u64 stack_top, u64 argument);
}
