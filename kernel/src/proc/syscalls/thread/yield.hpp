#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// yield - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 yield();
}
