#include <astia/api/errno.h>
#include <astia/api/signal.h>

#include "../../sys/aio.hpp"
#include "../mapping.hpp"
#include "../proc.hpp"
#include "../signal_permission.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// kill - send a signal to a process or process group
    ///
    /// Synopsis:
    ///     long kill(int pid, int signal);
    ///
    /// Description:
    ///     pid > 0 targets one process, pid == 0 targets the caller's
    ///     process group, and pid < 0 targets process group -pid. Signal
    ///     zero performs an existence check without enqueuing a signal.
    ///
    /// Return Value:
    ///     Returns zero on success.
    ///
    /// Errors:
    ///     -EINVAL: signal or target identifier is malformed.
    ///     -ESRCH: no matching live process or process group exists.
    struct kill
        : sys::syscall_handler<kill, sys::syscall_id::kill, sys::syscall_mode::sync> {
        static i64 invoke(i32 pid_raw, i32 signal_raw) {
            auto mapped_signal = signal_raw == 0
                                     ? optional<signal_number>(signal_number{})
                                     : mapper<signal_number>::from(signal_raw);
            if (!mapped_signal)
                return -EINVAL;

            if (pid_raw <= 0) {
                pgrp_t group = getpgrp();
                if (pid_raw < 0) {
                    auto mapped_group = mapper<pgrp_t>::from(-static_cast<i64>(pid_raw));
                    if (!mapped_group)
                        return -EINVAL;
                    group = mapped_group.value;
                }
                signal_info info{};
                if (mapped_signal.value != signal_number{}) {
                    auto* sender = current_process();
                    if (sender == nullptr)
                        return -ESRCH;
                    info.code = ASTIA_SI_USER;
                    info.pid  = static_cast<i32>(raw_id(sender->pid()));
                    info.uid  = sender->uid();
                }
                return signal_process_group(group, mapped_signal.value, info);
            }

            auto mapped_pid = mapper<pid_t>::from(pid_raw);
            if (!mapped_pid)
                return -EINVAL;
            auto target = retain_process(mapped_pid.value);
            if (!target)
                return -ESRCH;
            auto* sender = current_process();
            if (sender == nullptr)
                return -ESRCH;
            if (!signal_permission(sender->uid(), sender->euid(), target->uid(),
                                   target->euid()))
                return -EPERM;
            if (signal_raw == 0)
                return 0;
            signal_info info{};
            info.code = ASTIA_SI_USER;
            info.pid  = static_cast<i32>(raw_id(sender->pid()));
            info.uid  = sender->uid();
            if (!target->signals().send(mapped_signal.value, info))
                return -EINVAL;
            target->interrupt_signal_io(mapped_signal.value);
            if (mapped_signal.value == signal_number::kill) {
                target->signal_termination(signal_number::kill);
                target->request_kill();
            }
            else if (is_uncatchable(mapped_signal.value) ||
                     target->signals().disposition(mapped_signal.value).action ==
                         signal_action::default_action)
                target->signal_state_change(mapped_signal.value);
            if (target->signals().has_unblocked_interrupting())
                sys::io_interrupt_process(target.get(), -EINTR);
            target->wake_signal_waiters();
            return 0;
        }
    };

} // namespace astia::proc::syscalls
