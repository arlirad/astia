#include "sigprocmask.hpp"

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

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
