#pragma once
#include <astia/common.hpp>
#include <astia/arch/context.hpp>
namespace astia::proc::syscall {
    /// execve - Replace the calling process image.
    ///
    /// Synopsis:
    ///     long execve(const char *path, char *const argv[], char *const envp[]);
    ///
    /// Description:
    ///     Copies the argument and environment vectors, loads the executable,
    ///     and resumes the current process at its entry point. The process ID,
    ///     descriptor table, process group, and working directory remain.
    ///
    /// Return Value:
    ///     Does not return on success. Returns a negative errno on failure.
    ///
    /// Errors:
    ///     -E2BIG: The argument or environment vector exceeds its limit.
    ///     -EFAULT: A path, vector, or string pointer is invalid.
    ///     -ENOENT: The executable path does not exist.
    ///     -ENOEXEC: The file is not a loadable executable image.
    ///     -ENOMEM: The replacement image cannot be allocated.
    ///
    /// Notes:
    ///     Relative paths are resolved against the caller's working directory.
    i64 execve(const arch::syscall_frame& frame);
}
