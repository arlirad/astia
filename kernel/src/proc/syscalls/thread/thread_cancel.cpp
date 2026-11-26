#include <astia/api/errno.h>

#include "../../../ipc/futex.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_cancel
        : sys::syscall_handler<thread_cancel, sys::syscall_id::thread_cancel,
                               sys::syscall_mode::sync> {
        static i64 invoke(u32 tid) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            auto target = owner->thread_by_tid(tid);

            if (!target || target->state() == thread_state::dead)
                return -ESRCH;

            target->request_cancellation();
            target->wake_join_for_cancellation();

            const bool futex_woken = futex_interrupt_thread(target.get());

            if (!futex_woken && target->state() == thread_state::blocked) {
                (void) target->wake_wait();
                (void) scheduler::wake(target.get());
            }

            return 0;
        }
    };
} // namespace astia::proc::syscalls
