#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>
#include <astia/mm/share.hpp>

#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "../syscall_continuation.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
namespace astia::proc::syscalls::detail {

    namespace {
        struct spawn_continuation {
            syscall_continuation     syscall;
            mm::shptr<fs::open_file> file;
            argument_staging         staging;
            core::task<i64>          runner;

            spawn_continuation(const arch::syscall_frame&      input,
                               const mm::shptr<process>&       process_value,
                               const mm::shptr<thread>&        thread_value,
                               const mm::shptr<fs::open_file>& file_value)
                : syscall(input, process_value, thread_value), file(file_value) {}
        };

        core::task<i64> drive_spawn(const mm::shptr<spawn_continuation>& continuation) {
            auto result = co_await proc::spawnve(
                continuation->syscall.owner(), continuation->file,
                static_cast<usize>(continuation->file->size()),
                continuation->staging.argv(), continuation->staging.envp());
            if (!result) {
                const i64 error = -astia::errno_value(result.error);
                continuation->syscall.complete(error);
                co_return result.error;
            }
            const i64 pid = static_cast<i64>(raw_id(result.value->pid()));
            continuation->syscall.complete(pid);
            co_return pid;
        }
    }

    i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr) {
        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;

        char path[path_cap];
        if (!sys::copy_in_string(span<char>(path, sizeof path),
                                 sys::user_ptr<char>(path_ptr))) {
            return -EFAULT;
        }

        auto staging = mm::share<spawn_continuation>(
            arch::syscall_frame{}, mm::shptr<process>{}, mm::shptr<thread>{},
            mm::shptr<fs::open_file>{});
        if (!staging)
            return -ENOMEM;
        if (!staging->staging.allocate())
            return -ENOMEM;
        i64 result = staging->staging.copy(argv_ptr, envp_ptr);
        if (result != 0)
            return result;

        const fs::file_mode image_mode = fs::node_mode(path);
        if (image_mode.exists() &&
            !owner->can_access(image_mode, fs::node_uid(path), fs::node_gid(path),
                               fs::permission_bits::execute))
            return -EACCES;
        auto file = fs::open_path(path);
        if (!file)
            return -ENOENT;
        const long image_size = file->size();
        if (image_size <= 0)
            return -EINVAL;

        auto owner_ref = retain_process(owner->pid());
        auto self_ref  = owner->retain_thread(self);
        if (!owner_ref || !self_ref)
            return -ESRCH;
        staging->syscall.bind(owner_ref, self_ref);
        staging->file = file;

        staging->runner = drive_spawn(staging);
        if (!staging->runner.valid())
            return -ENOMEM;
        staging->runner.resume();
        if (!staging->runner.done() && !staging->syscall.wait()) {
            staging->runner.reset();
            return staging->syscall.result();
        }
        while (!staging->runner.done())
            scheduler::yield();

        result = staging->syscall.result();
        staging->runner.reset();
        return result;
    }
}

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
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return ::astia::proc::syscalls::detail::spawnve(
                static_cast<u64>(context.arguments[0]),
                static_cast<u64>(context.arguments[1]),
                static_cast<u64>(context.arguments[2]));
        }
    };

    extern const sys::syscall_descriptor spawnve_descriptor = spawnve::descriptor;

} // namespace astia::proc::syscalls
