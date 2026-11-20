#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// getppid - Return the parent process identifier.
///
/// Synopsis:
///     long getppid(void);
///
/// Return Value:
///     Returns the parent process identifier or a negative errno.
namespace astia::proc::syscall {
    i64 getppid();
}
