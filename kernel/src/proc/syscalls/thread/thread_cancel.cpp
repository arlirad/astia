#include "thread_cancel.hpp"

#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "../../../ipc/futex.hpp"
#include <astia/api/errno.h>

namespace astia::proc::syscall {

    i64 thread_cancel(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto target = owner->thread_by_tid(tid);
        if (!target || target->state() == thread_state::dead)
            return -ESRCH;
        target->request_cancellation();
        target->wake_join_for_cancellation();
        (void) futex_interrupt_thread(target.get());
        return 0;
    }

}
