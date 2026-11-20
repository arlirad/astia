#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    /// getpid - return the calling process ID.
    ///
    /// Synopsis:
    ///     long getpid(void);
    ///
    /// Return Value:
    ///     The calling process ID.
    i64 getpid();
}
