#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// futex - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 futex(u64 address, u32 op, u32 expected, u64 timeout);
}
