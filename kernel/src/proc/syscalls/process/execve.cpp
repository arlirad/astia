#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>

#include "../../proc.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscalls {

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
    ///     The image load is a task; the dispatcher parks the calling thread
    ///     until it completes, and the final resume writes the entry point and
    ///     thread pointer back into the blocked thread.
    struct execve : sys::syscall_handler<execve, sys::syscall_id::execve,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_trap        trap,
                                      sys::user_ptr<char>      path_ptr,
                                      sys::user_ptr<const u64> argv_ptr,
                                      sys::user_ptr<const u64> envp_ptr) {
            char path[detail::path_cap];
            if (!sys::copy_in_string(span<char>(path, sizeof path), path_ptr))
                co_return -EFAULT;

            detail::argument_staging staging;
            if (!staging.allocate())
                co_return -ENOMEM;
            const i64 copied = staging.copy(argv_ptr.address(), envp_ptr.address());
            if (copied != 0)
                co_return copied;

            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr)
                co_return -ESRCH;
            auto file = fs::open_path(path);
            if (!file)
                co_return -ENOENT;
            const long size = file->size();
            if (size <= 0)
                co_return -ENOEXEC;

            auto owner_ref = retain_process(owner->pid());
            auto self_ref  = owner->retain_thread(self);
            if (!owner_ref || !self_ref)
                co_return -ESRCH;

            auto result = co_await owner_ref->exec_user_file_async(
                static_cast<mm::shptr<fs::open_file>&&>(file), static_cast<usize>(size),
                staging.argv(), staging.envp());
            if (!result)
                co_return -errno_value(result.error);

            arch::interrupt_guard gate;
            self_ref->user_stack_top(result.value.stack);
            self_ref->user_stack_metadata(user_stack_top - user_stack_bytes,
                                          user_stack_bytes);
            arch::context_set_fs_base(self_ref->context(), owner_ref->tls_tp());
            trap.image.set_resume_point(result.value.entry, result.value.stack, 0x202);
            co_return 0;
        }
    };

} // namespace astia::proc::syscalls
