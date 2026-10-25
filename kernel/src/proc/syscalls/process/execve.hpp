#pragma once
#include <astia/common.hpp>
#include <astia/arch/context.hpp>
namespace astia::proc::syscall {
    /// execve - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 execve(const arch::syscall_frame& frame);
}
