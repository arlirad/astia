#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// chdir - change the caller's working directory.
    ///
    /// Synopsis:
    ///     long chdir(const char *path);
    ///
    /// Errors:
    ///     -EFAULT: The path pointer is inaccessible.
    ///     -ENOTDIR: The path does not identify a directory.
    ///     -ENOENT: The directory does not exist.
    ///     -ESRCH: There is no current process.
    i64 chdir(u64 path);
}
