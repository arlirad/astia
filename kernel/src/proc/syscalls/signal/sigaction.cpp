#include "sigaction.hpp"
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



        i64 sigaction(i32 value, u64 next_ptr, u64 old_ptr) {
            auto mapped_signal = mapper<signal_t>::from(value);
            if (!mapped_signal || !valid_signal(mapped_signal.value)) return -EINVAL;
            const auto signal = mapped_signal.value;
            auto*      owner  = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            const auto old = owner->signals().disposition(signal);
            if (old_ptr != 0) {
                const auto range =
                    sys::user_range::bytes(old_ptr, sizeof(struct astia_sigaction));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                struct astia_sigaction out{};
                out.handler  = old.action == signal_action::ignore ? ASTIA_SIG_IGN
                               : old.action == signal_action::handler
                                   ? reinterpret_cast<uintptr_t>(old.handler)
                                   : ASTIA_SIG_DFL;
                out.mask     = old.mask;
                out.flags    = 0;
                out.restorer = old.restorer;
                if (!sys::copy_out(range, &out)) {
                    return -EFAULT;
                }
            }
            if (next_ptr != 0) {
                const auto range =
                    sys::user_range::bytes(next_ptr, sizeof(struct astia_sigaction));
                if (!sys::user_range_ok(range)) {
                    return -EFAULT;
                }
                struct astia_sigaction next{};
                if (!sys::copy_in(&next, range)) {
                    return -EFAULT;
                }
                signal_disposition disposition{};
                if (next.handler == ASTIA_SIG_IGN) {
                    disposition.action = signal_action::ignore;
                }
                else if (next.handler != ASTIA_SIG_DFL) {
                    disposition.action   = signal_action::handler;
                    disposition.handler  = reinterpret_cast<signal_handler>(next.handler);
                    disposition.restorer = next.restorer;
                    disposition.mask     = next.mask;
                    if (disposition.restorer == 0 ||
                        !sys::user_range_ok(
                            sys::user_range::bytes(disposition.restorer, 1)))
                        return -EFAULT;
                }
                if (!owner->signals().disposition(signal, disposition)) {
                    return -EINVAL;
                }
            }
            return 0;
        }

} // namespace astia::proc::syscall
