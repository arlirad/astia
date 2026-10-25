#pragma once
#include <astia/common.hpp>
#include <astia/arch/context.hpp>
namespace astia::proc::syscall {
    /// fork - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 fork(const arch::syscall_frame& frame);
}
