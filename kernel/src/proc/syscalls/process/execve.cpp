#include <astia/api/errno.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/mapping.hpp>

#include "../../exec_permission.hpp"
#include "../../proc.hpp"
#include "argv.hpp"
#include "path_staging.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscalls {
    struct execve : sys::syscall_handler<execve, sys::syscall_id::execve,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_trap        trap,
                                      sys::user_ptr<char>      path_ptr,
                                      sys::user_ptr<const u64> argv_ptr,
                                      sys::user_ptr<const u64> envp_ptr) {
            detail::path_staging path{path_ptr};

            const i64 staged = path.copy();
            if (staged != 0)
                co_return staged;

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

            char      resolved[fs::path_max]{};
            const i64 resolve_rc =
                owner->resolve_path(path.c_str(), resolved, sizeof resolved, true);
            if (resolve_rc < 0)
                co_return resolve_rc;

            const i64 permission_rc = exec_path_check(*owner, resolved);
            if (permission_rc < 0)
                co_return permission_rc;

            auto file = fs::open_path(resolved);
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
                static_cast<mm::shptr<fs::open_file>&&>(file), static_cast<usz>(size),
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
