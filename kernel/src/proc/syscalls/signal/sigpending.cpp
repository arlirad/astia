#include "sigpending.hpp"

#include "../../proc.hpp"
#include "user_mem.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {

    i64 sigpending(u64 set_ptr) {
        auto*      owner = current_process();
        auto*      self  = current_thread();
        const auto range = sys::user_ptr<u32>(set_ptr).validate();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;
        if (!range)
            return -EFAULT;
        const signal_mask blocked = self->signals().blocked();
        const signal_mask pending =
            (self->signals().pending() | owner->signals().pending()) & blocked;
        const u32 value = pending;
        return sys::copy_out(range.value.bytes(), &value) ? 0 : -EFAULT;
    }

}
