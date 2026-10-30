#include "sigsuspend.hpp"
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


        i64 sigsuspend(u64 mask_ptr) {
            auto* owner = current_process();
            auto* self  = current_thread();
            if (owner == nullptr || self == nullptr)
                return -ESRCH;
            const auto range = sys::user_range::bytes(mask_ptr, sizeof(u32));
            if (!sys::user_range_ok(range))
                return -EFAULT;
            u32 mask = 0;
            if (!sys::copy_in(&mask, range))
                return -EFAULT;
            // Match waitpid's register-before-scan protocol.  Interrupts stay
            // masked until either a pending signal is observed or this thread
            // is blocked, so delivery cannot fall between the check and the
            // waiter registration.
            arch::cpu::nointerrupts();
            const u32 previous_mask = owner->signals().blocked();
            owner->signals().set_blocked(mask);
            if (!owner->add_signal_waiter(self)) {
                owner->signals().set_blocked(previous_mask);
                arch::cpu::interrupts();
                return -EAGAIN;
            }
            while (!owner->signals().has_unblocked_pending()) {
                // Signal delivery is performed by the syscall exit hook.
                // Signal senders clear the registration before waking us.
                self->state(thread_state::blocked);
                scheduler::yield();
            }
            owner->remove_signal_waiter(self);
            // POSIX sigsuspend changes the mask only for the sleep.  Leaving
            // the temporary mask installed loses SIGCHLD after zsh returns
            // from its native signal wait and makes the next job reap hang.
            self->signal_restore_mask(previous_mask);
            arch::cpu::interrupts();
            return -EINTR;
        }

} // namespace astia::proc::syscall
