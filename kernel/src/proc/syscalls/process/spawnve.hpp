#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// spawnve - Astia process syscall.
    ///
    /// Return Value:
    ///     Returns zero or a non-negative result on success.
    i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr);
}
