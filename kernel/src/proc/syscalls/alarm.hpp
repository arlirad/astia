#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// alarm - Schedule delivery of a process alarm.
///
/// Synopsis:
///     long alarm(long seconds);
///
/// Return Value:
///     Returns the previous alarm value or a negative errno.
namespace astia::proc::syscall {
    i64 alarm(i64 seconds);
}
