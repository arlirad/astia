#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscall {

    i64 sigprocmask(i32 how, signal_mask set, u64 old_ptr) {
        auto* self = current_thread();
        if (self == nullptr) {
            return -ESRCH;
        }
        auto operation = mapper<signal_mask_operation>::from(how);
        if (!operation)
            return -EINVAL;
        if (old_ptr != 0) {
            const auto old_range = sys::user_ptr<u32>(old_ptr).validate();
            const u32  old       = self->signals().blocked();
            if (!old_range || !sys::copy_out(old_range.value.bytes(), &old)) {
                return -EFAULT;
            }
        }
        switch (operation.value) {
        case signal_mask_operation::block:
            self->signals().block(set);
            break;
        case signal_mask_operation::unblock:
            self->signals().unblock(set);
            break;
        case signal_mask_operation::setmask:
            self->signals().set_blocked(set);
            break;
        }
        return 0;
    }

} // namespace astia::proc::syscall

namespace astia::proc::syscalls {

    /// sigprocmask - block, unblock, or replace the signal mask.
    ///
    /// Synopsis:
    ///     long sigprocmask(int how, uint32_t set, uint32_t *old);
    ///
    /// Errors:
    ///     -EFAULT: The old mask pointer is inaccessible.
    ///     -EINVAL: The mask operation is unknown.
    ///     -ESRCH: There is no current process.
    struct sigprocmask : sys::syscall_handler<sigprocmask, sys::syscall_id::sigprocmask,
                                              sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::sigprocmask>(context);
        }
    };

    extern const sys::syscall_descriptor sigprocmask_descriptor = sigprocmask::descriptor;

} // namespace astia::proc::syscalls
