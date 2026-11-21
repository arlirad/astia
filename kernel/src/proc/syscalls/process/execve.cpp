#include "execve.hpp"

#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>
#include <astia/mm/share.hpp>

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "../syscall_continuation.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
namespace astia::proc::syscall {
    using namespace detail;

    namespace {
        struct exec_continuation {
            syscall_continuation     syscall;
            mm::shptr<fs::open_file> file;
            argument_staging         staging;
            u64                      entry = 0;
            u64                      stack = 0;
            core::task<exec_result>  runner;

            exec_continuation(const arch::syscall_frame&      input,
                              const mm::shptr<process>&       process_value,
                              const mm::shptr<thread>&        thread_value,
                              const mm::shptr<fs::open_file>& file_value)
                : syscall(input, process_value, thread_value), file(file_value) {}

            void complete(i64 value, const exec_result* image = nullptr) {
                if (image != nullptr) {
                    entry = image->entry;
                    stack = image->stack;
                }
                syscall.complete(value);
            }
        };

        core::task<exec_result>
        drive_exec(const mm::shptr<exec_continuation>& continuation) {
            auto result = co_await static_cast<core::task<exec_result>&&>(
                continuation->syscall.owner()->exec_user_file_async(
                    continuation->file, static_cast<usize>(continuation->file->size()),
                    continuation->staging.argv(), continuation->staging.envp()));
            if (!result) {
                continuation->complete(-errno_value(result.error));
                co_return result.error;
            }
            continuation->complete(0, &result.value);
            co_return result.value;
        }
    }

    i64 execve(const arch::syscall_frame& frame) {
        const arch::syscall_frame saved_frame = frame;
        const u64                 path_ptr    = saved_frame.a0;
        const u64                 argv_ptr    = saved_frame.a1;
        const u64                 envp_ptr    = saved_frame.a2;
        char                      path[path_cap];
        if (!sys::copy_in_string(span<char>(path, sizeof path),
                                 sys::user_ptr<char>(path_ptr)))
            return -EFAULT;
        auto staging =
            mm::share<exec_continuation>(saved_frame, mm::shptr<process>{},
                                         mm::shptr<thread>{}, mm::shptr<fs::open_file>{});
        if (!staging)
            return -ENOMEM;
        if (!staging->staging.allocate())
            return -ENOMEM;
        i64 result = staging->staging.copy(argv_ptr, envp_ptr);
        if (result != 0)
            return result;

        auto* owner = current_process();
        auto* self  = current_thread();
        auto  file  = fs::open_path(path);
        if (owner == nullptr || self == nullptr || !file)
            return -ENOENT;
        const long size = file->size();
        if (size <= 0)
            return -ENOEXEC;

        auto owner_ref = retain_process(owner->pid());
        auto self_ref  = owner->retain_thread(self);
        if (!owner_ref || !self_ref)
            return -ESRCH;
        staging->syscall.bind(owner_ref, self_ref);
        staging->file = file;

        staging->runner = drive_exec(staging);
        if (!staging->runner.valid())
            return -ENOMEM;
        staging->runner.resume();
        if (!staging->runner.done())
            (void) staging->syscall.wait();

        while (!staging->runner.done())
            scheduler::yield();

        result = staging->syscall.result();
        staging->runner.reset();
        if (result == 0) {
            arch::interrupt_guard gate;
            self->user_stack_top(staging->stack);
            self->user_stack_metadata(user_stack_top - user_stack_bytes,
                                      user_stack_bytes);
            arch::context_set_fs_base(self->context(), owner->tls_tp());
            auto& next  = const_cast<arch::syscall_frame&>(frame);
            next.rip    = staging->entry;
            next.rsp    = staging->stack;
            next.rflags = 0x202;
        }
        return result;
    }
}

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

    /// ABI ID: execve.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct execve : sys::syscall_handler<execve, sys::syscall_id::execve,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor execve_descriptor = execve::descriptor;

} // namespace astia::proc::syscalls
