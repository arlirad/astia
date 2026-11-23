#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>

#include "../../proc.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscalls {

    /// spawnve - Create a process using asynchronous streamed ELF loading.
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
    ///     -EINVAL: The file is malformed or truncated.
    ///     -ENOMEM: The image or process cannot be allocated.
    ///     -EAGAIN: The process or thread registry is full.
    struct spawnve : sys::syscall_handler<spawnve, sys::syscall_id::spawnve,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<char>      path_ptr,
                                      sys::user_ptr<const u64> argv_ptr,
                                      sys::user_ptr<const u64> envp_ptr) {
            auto* owner = current_process();
            if (owner == nullptr || current_thread() == nullptr)
                co_return -ESRCH;

            char path[detail::path_cap];
            if (!sys::copy_in_string(span<char>(path, sizeof path), path_ptr))
                co_return -EFAULT;

            detail::argument_staging staging;
            if (!staging.allocate())
                co_return -ENOMEM;
            const i64 copied = staging.copy(argv_ptr.address(), envp_ptr.address());
            if (copied != 0)
                co_return copied;

            const fs::file_mode image_mode = fs::node_mode(path);
            if (image_mode.exists() &&
                !owner->can_access(image_mode, fs::node_uid(path), fs::node_gid(path),
                                   fs::permission_bits::execute))
                co_return -EACCES;
            auto file = fs::open_path(path);
            if (!file)
                co_return -ENOENT;
            const long image_size = file->size();
            if (image_size <= 0)
                co_return -EINVAL;

            auto child = co_await astia::proc::spawnve(
                owner, static_cast<mm::shptr<fs::open_file>&&>(file),
                static_cast<usize>(image_size), staging.argv(), staging.envp());
            if (!child)
                co_return -astia::errno_value(child.error);
            co_return static_cast<i64>(raw_id(child.value->pid()));
        }
    };

} // namespace astia::proc::syscalls
