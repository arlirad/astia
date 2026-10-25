#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_join - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 thread_join(u32 tid, u64 result_ptr);
}
