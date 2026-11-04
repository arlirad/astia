#include "execve.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "argv.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>
#include <astia/mm/share.hpp>
namespace astia::proc::syscall {
    using namespace detail;

    namespace {
        struct exec_continuation {
            arch::syscall_frame       frame;
            mm::shptr<process>        owner;
            mm::shptr<thread>         waiter;
            mm::shptr<fs::open_file>  file;
            argument_staging          staging;
            i64                       result = -EIO;
            u64                       entry  = 0;
            u64                       stack  = 0;
            u8                        completed = 0;
            bool                      parked = false;
            core::task<exec_result>  runner;

            exec_continuation(const arch::syscall_frame& input,
                              const mm::shptr<process>& process_value,
                              const mm::shptr<thread>& thread_value,
                              const mm::shptr<fs::open_file>& file_value)
                : frame(input), owner(process_value), waiter(thread_value), file(file_value) {}

            void complete(i64 value, const exec_result* image = nullptr) {
                result = value;
                if (image != nullptr) {
                    entry = image->entry;
                    stack = image->stack;
                }
                __atomic_store_n(&completed, static_cast<u8>(1), __ATOMIC_RELEASE);
                if (parked)
                    scheduler::wake(waiter.get());
            }
        };

        core::task<exec_result> drive_exec(const mm::shptr<exec_continuation>& continuation) {
            auto result = co_await static_cast<core::task<exec_result>&&>(
                continuation->owner->exec_user_file_async(
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
        const u64                 path_ptr   = saved_frame.a0;
        const u64                 argv_ptr   = saved_frame.a1;
        const u64                 envp_ptr   = saved_frame.a2;
        char                  path[path_cap];
        if (!sys::copy_in_string(span<char>(path, sizeof path),
                                 sys::user_ptr<char>(path_ptr)))
            return -EFAULT;
        auto staging = mm::share<exec_continuation>(saved_frame,
                                                     mm::shptr<process>{},
                                                     mm::shptr<thread>{},
                                                     mm::shptr<fs::open_file>{});
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
        staging->owner  = owner_ref;
        staging->waiter = self_ref;
        staging->file   = file;

        staging->runner = drive_exec(staging);
        if (!staging->runner.valid())
            return -ENOMEM;
        staging->runner.resume();
        if (!staging->runner.done()) {
            staging->parked = true;
            if (__atomic_load_n(&staging->completed, __ATOMIC_ACQUIRE)) {
                (void) scheduler::wake(self);
            }
            else {
                self->state(thread_state::blocked);
                scheduler::yield();
            }
        }

        result = staging->result;
        staging->runner.reset();
        if (result == 0) {
            arch::interrupt_guard gate;
            self->user_stack_top(staging->stack);
            arch::context_update_fs_base(self->context(), owner->tls_tp());
            auto& next  = const_cast<arch::syscall_frame&>(frame);
            next.rip    = staging->entry;
            next.rsp    = staging->stack;
            next.rflags = 0x202;
        }
        return result;
    }
}
