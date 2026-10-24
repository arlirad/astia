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


        i64 sigprocmask(i32 how, u32 set, u64 old_ptr) {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            if (old_ptr != 0) {
                const auto range = sys::user_range::bytes(old_ptr, sizeof(u32));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                const u32 old = owner->signals().blocked();
                if (!sys::copy_out(range, &old)) {
                    return -EFAULT;
                }
            }
            auto operation = mapper<signal_mask_operation>::from(how);
            if (!operation) return -EINVAL;
            switch (operation.value) {
            case signal_mask_operation::block:
                owner->signals().block(set);
                break;
            case signal_mask_operation::unblock:
                owner->signals().unblock(set);
                break;
            case signal_mask_operation::setmask:
                owner->signals().set_blocked(set);
                break;
            }
            return 0;
        }

} // namespace astia::proc::syscall
