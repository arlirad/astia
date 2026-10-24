#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// exit - terminate the calling thread.
    ///
    /// Synopsis:
    ///     long exit(long status);
    ///
    /// Return Value:
    ///     Does not return to the caller.
    i64 exit(i64 status);
}
