#include "sigsuspend.hpp"

#include <astia/api/errno.h>
#include <astia/api/signal.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"

namespace astia::proc::syscall {

    i64 sigsuspend(u64 mask_ptr) {
        auto* self  = current_thread();
        auto* owner = current_process();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;
        const auto range = sys::user_ptr<u32>(mask_ptr).validate();
        if (!range)
            return -EFAULT;
        signal_mask mask = 0;
        if (!sys::copy_in(&mask, range.value.bytes()))
            return -EFAULT;
        // Match waitpid's register-before-scan protocol.  Interrupts stay
        // masked until either a pending signal is observed or this thread
        // is blocked, so delivery cannot fall between the check and the
        // waiter registration.
        arch::cpu::nointerrupts();
        const signal_mask previous_mask = self->signals().blocked();
        self->signals().set_blocked(mask);
        if (!owner->add_signal_waiter(self)) {
            self->signals().set_blocked(previous_mask);
            arch::cpu::interrupts();
            return -EAGAIN;
        }
        while (!self->signals().has_unblocked_pending() &&
               !owner->signals().has_pending_for(~self->signals().blocked())) {
            if (owner->block_signal_waiter(self))
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

namespace astia::proc::syscalls {

    /// ABI ID: sigsuspend.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct sigsuspend : sys::syscall_handler<sigsuspend, sys::syscall_id::sigsuspend,
                                             sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sigsuspend_descriptor = sigsuspend::descriptor;

} // namespace astia::proc::syscalls
