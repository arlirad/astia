#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// getcwd - copy the caller's working directory.
    ///
    /// Synopsis:
    ///     long getcwd(char *buffer, size_t length);
    ///
    /// Errors:
    ///     -EFAULT: The output buffer is inaccessible.
    ///     -ERANGE: The buffer is too small.
    ///     -ESRCH: There is no current process.
    i64 getcwd(u64 buffer, u64 length);
}
