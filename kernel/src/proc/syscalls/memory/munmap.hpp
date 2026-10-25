#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// munmap - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 munmap(u64 addr, i64 len);
}
