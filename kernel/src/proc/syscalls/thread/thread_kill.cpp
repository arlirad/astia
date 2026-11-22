#include <astia/api/errno.h>

#include "../../../ipc/futex.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_kill : sys::syscall_handler<thread_kill, sys::syscall_id::thread_kill,
                                              sys::syscall_mode::sync> {
        static i64 invoke(u32 tid, i32 raw_signal) {
            auto mapped_signal = raw_signal == 0
                                     ? optional<signal_number>(signal_number{})
                                     : mapper<signal_number>::from(raw_signal);
            if (!mapped_signal || (raw_signal != 0 && !valid_signal(mapped_signal.value)))
                return -EINVAL;
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;
            auto target = owner->thread_by_tid(tid);
            if (!target || target->state() == thread_state::dead)
                return -ESRCH;
            if (raw_signal == 0)
                return 0;

            const signal_number signal      = mapped_signal.value;
            const auto          disposition = owner->signals().disposition(signal);
            signal_info         info{};
            info.pid = static_cast<i32>(raw_id(owner->pid()));
            info.uid = owner->uid();
            if (!target->signals().send_pending(signal, info))
                return -EINVAL;
            const bool interruptible =
                disposition.action == signal_action::handler
                    ? (disposition.flags & ASTIA_SA_RESTART) == 0
                    : disposition.action == signal_action::default_action &&
                          default_action(signal) != signal_default::ignore;
            const bool futex_woken =
                interruptible &&
                (target->signals().blocked() & signal_bit(signal)) == 0 &&
                futex_interrupt_thread(target.get());
            if (signal == signal_number::kill) {
                owner->signal_termination(signal_number::kill);
                owner->request_kill();
            }
            else if (is_uncatchable(signal) ||
                     disposition.action == signal_action::default_action)
                owner->signal_state_change(signal);
            owner->wake_signal_waiters();
            if (!futex_woken && target->state() == thread_state::blocked)
                scheduler::wake(target.get());
            return 0;
        }
    };

    extern const sys::syscall_descriptor thread_kill_descriptor = thread_kill::descriptor;

} // namespace astia::proc::syscalls
