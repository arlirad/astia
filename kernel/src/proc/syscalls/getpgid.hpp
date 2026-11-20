#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// getpgid - Return a process group identifier.
///
/// Synopsis:
///     long getpgid(int pid);
///
/// Return Value:
///     Returns the process group identifier or a negative errno.
namespace astia::proc::syscall {
    i64 getpgid(i64 target);
}
