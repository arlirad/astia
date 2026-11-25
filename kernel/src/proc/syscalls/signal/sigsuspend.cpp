#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "signal_park.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct sigsuspend : sys::syscall_handler<sigsuspend, sys::syscall_id::sigsuspend,
                                             sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::user_ptr<u32> mask_ptr) {
            auto* self  = current_thread();
            auto* owner = current_process();
            if (owner == nullptr || self == nullptr)
                co_return -ESRCH;
            const auto range = mask_ptr.validate();
            if (!range)
                co_return -EFAULT;
            signal_mask mask = 0;
            if (!sys::copy_in(&mask, range.value.bytes()))
                co_return -EFAULT;

            // Match waitpid's register-before-scan protocol.  Interrupts stay
            // masked until either a pending signal is observed or this thread
            // is blocked, so delivery cannot fall between the check and the
            // waiter registration.
            const signal_mask previous_mask = self->signals().blocked();
            {
                arch::interrupt_guard gate;
                self->signals().set_blocked(mask);
                if (!owner->add_signal_waiter(self)) {
                    self->signals().set_blocked(previous_mask);
                    co_return -EAGAIN;
                }
            }
            for (;;) {
                arch::interrupt_guard gate;
                if (self->cancellation_pending()) {
                    owner->remove_signal_waiter(self);
                    self->signal_restore_mask(previous_mask);
                    co_return -EINTR;
                }
                if (self->signals().has_unblocked_pending() ||
                    owner->signals().has_pending_for(~self->signals().blocked()))
                    break;
                co_await signal_park{*owner, *self, gate};
            }
            owner->remove_signal_waiter(self);
            // POSIX sigsuspend changes the mask only for the sleep.  Leaving
            // the temporary mask installed loses SIGCHLD after zsh returns
            // from its native signal wait and makes the next job reap hang.
            self->signal_restore_mask(previous_mask);
            co_return -EINTR;
        }
    };

} // namespace astia::proc::syscalls
