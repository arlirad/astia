#include "thread_exists.hpp"

#include "../../proc.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {

    i64 thread_exists(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto target = owner->thread_by_tid(tid);
        return !target || target->state() == thread_state::dead ? -ESRCH : 0;
    }

}
