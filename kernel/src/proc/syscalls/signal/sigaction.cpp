#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    namespace {
        uintptr_t user_handler_value(const signal_disposition& disposition) {
            switch (disposition.action) {
            case signal_action::ignore:
                return ASTIA_SIG_IGN;
            case signal_action::handler:
                return reinterpret_cast<uintptr_t>(disposition.handler);
            case signal_action::default_action:
                return ASTIA_SIG_DFL;
            }
            return ASTIA_SIG_DFL;
        }
    }

    /// sigaction - get or change a signal disposition.
    ///
    /// Synopsis:
    ///     long sigaction(int signal, const action *next, action *old);
    ///
    /// Errors:
    ///     -EFAULT: An action pointer or signal restorer is inaccessible.
    ///     -EINVAL: The signal is invalid or cannot be caught.
    ///     -ESRCH: There is no current process.
    struct sigaction : sys::syscall_handler<sigaction, sys::syscall_id::sigaction,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i32  value         = static_cast<i32>(context.arguments[0]);
            u64  next_ptr      = static_cast<u64>(context.arguments[1]);
            u64  old_ptr       = static_cast<u64>(context.arguments[2]);
            auto mapped_signal = mapper<signal_number>::from(value);
            if (!mapped_signal || !valid_signal(mapped_signal.value))
                return -EINVAL;
            const auto signal = mapped_signal.value;
            auto*      owner  = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }

            // Materialize and validate the requested disposition before producing
            // old-action output.  Besides giving invalid actions precedence over
            // a bad old pointer, this keeps a failed change from having any
            // observable output or state effect.
            signal_disposition next_disposition{};
            if (next_ptr != 0) {
                const auto next_range =
                    sys::user_ptr<struct astia_sigaction>(next_ptr).validate();
                struct astia_sigaction next{};
                if (!next_range || !sys::copy_in(&next, next_range.value.bytes())) {
                    return -EFAULT;
                }
                if (next.handler == ASTIA_SIG_IGN) {
                    next_disposition.action = signal_action::ignore;
                }
                else if (next.handler != ASTIA_SIG_DFL) {
                    next_disposition.action = signal_action::handler;
                    next_disposition.handler =
                        reinterpret_cast<signal_handler>(next.handler);
                    next_disposition.restorer = next.restorer;
                    next_disposition.mask     = next.mask;
                    next_disposition.flags    = next.flags;
                    if (next_disposition.restorer == 0 ||
                        !sys::user_code_ptr_ok(next_disposition.restorer))
                        return -EFAULT;
                }
                if (is_uncatchable(signal))
                    return -EINVAL;
            }

            const auto old = owner->signals().disposition(signal);
            if (old_ptr != 0) {
                const auto old_range =
                    sys::user_ptr<struct astia_sigaction>(old_ptr).validate();
                struct astia_sigaction out{};
                out.handler  = user_handler_value(old);
                out.mask     = old.mask;
                out.flags    = old.flags;
                out.restorer = old.restorer;
                if (!old_range || !sys::copy_out(old_range.value.bytes(), &out)) {
                    return -EFAULT;
                }
            }
            if (next_ptr != 0 &&
                !owner->signals().change_disposition(signal, next_disposition))
                return -EINVAL;
            return 0;
        }
    };

    extern const sys::syscall_descriptor sigaction_descriptor = sigaction::descriptor;

} // namespace astia::proc::syscalls
