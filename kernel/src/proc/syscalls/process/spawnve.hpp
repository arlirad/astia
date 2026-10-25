#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// spawnve - Create a process from an executable image.
    ///
    /// Synopsis:
    ///     long spawnve(const char *path, char *const argv[], char *const envp[]);
    ///
    /// Description:
    ///     Copies the vectors, creates a child process, and builds its initial
    ///     user stack. The child inherits the caller's descriptors and cwd.
    ///
    /// Return Value:
    ///     Returns the child PID, or a negative errno.
    ///
    /// Errors:
    ///     -E2BIG: The argument or environment vector exceeds its limit.
    ///     -EFAULT: A path, vector, or string pointer is invalid.
    ///     -ENOENT: The executable path does not exist.
    ///     -ENOEXEC: The file is not a loadable executable image.
    ///     -ENOMEM: The image or process cannot be allocated.
    ///     -EAGAIN: The process or thread registry is full.
    i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr);
}
