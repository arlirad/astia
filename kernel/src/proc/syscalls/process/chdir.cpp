#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

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
    struct chdir
        : sys::syscall_handler<chdir, sys::syscall_id::chdir, sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<char> path) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;
            char copy[fs::path_max]{};
            if (!sys::copy_in_string(span<char>(copy, sizeof copy), path))
                return -EFAULT;
            return owner->change_directory(copy);
        }
    };

} // namespace astia::proc::syscalls
