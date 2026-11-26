#include "task_run.hpp"

#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/mapping.hpp>

#include "proc.hpp"
#include "scheduler.hpp"

namespace astia::proc {

    namespace {
        bool wait_live(const mm::shptr<process>& owner, const mm::shptr<thread>& self) {
            if (!owner || !self || self->state() == thread_state::dead)
                return false;
            const process_state state = owner->state();
            return state != process_state::zombie && state != process_state::dead;
        }

        void wait_complete(volatile bool& done, core::spinlock& lock,
                           const mm::shptr<thread>& self) {
            arch::interrupt_spinlock_guard guard(lock);
            if (done)
                return;
            done = true;
            // A running thread cannot be `blocked`, so this only fires when the
            // waiter is actually parked; waking a dead thread is rejected by
            // scheduler::wake.
            if (self->state() == thread_state::blocked)
                (void) scheduler::wake(self.get());
        }

        bool wait_park(volatile bool& done, core::spinlock& lock,
                       const mm::shptr<process>& owner, const mm::shptr<thread>& self) {
            for (;;) {
                // A wait queue may have handed this thread a continuation to
                // run on its own stack.  Do that before parking again.
                self->resume_pending();
                bool parked = false;
                {
                    arch::interrupt_spinlock_guard guard(lock);
                    if (done)
                        return true;
                    // A cancelled thread must not sit parked behind a long
                    // module load or close drain.  The waiter owns the
                    // operation coroutine; returning here detaches it from
                    // any completion and destroys the frame.
                    if (self->cancellation_pending())
                        return false;
                    if (!wait_live(owner, self))
                        return false;
                    // A waiter wakeup that lands after resume_pending() but
                    // before this block deposits its continuation here.  Parking
                    // anyway would lose it: the thread would sleep with the
                    // continuation queued and nothing left to wake it.
                    parked = self->park_unless_woken();
                }
                // A completer may run between the unlock and the yield; it sees
                // the blocked state under the same lock and wakes this thread.
                if (parked)
                    scheduler::yield();
            }
        }

        /// Runs when the handler task finishes; its return value is the
        /// syscall result.  The retained refs are copied into the coroutine
        /// frame so they outlive this call.
        core::task<i64> wake_when_done(core::task<i64> operation, volatile bool& done,
                                       core::spinlock& lock, mm::shptr<thread> self) {
            auto      completed = co_await static_cast<core::task<i64>&&>(operation);
            const i64 value = completed ? completed.value : -errno_value(completed.error);
            wait_complete(done, lock, self);
            co_return value;
        }

        /// Boot completion state.  The boot body starts before the process
        /// layer exists, so the wake target is published only after the first
        /// suspension, once the seed thread can be parked.
        struct boot_wait {
            core::spinlock    lock;
            volatile bool     done  = false;
            error_t           error = error_t::enone;
            mm::shptr<thread> self;
        };

        core::task<void> wake_boot_when_done(core::task<void> operation,
                                             boot_wait&       wait) {
            const error_t result = co_await static_cast<core::task<void>&&>(operation);
            arch::interrupt_spinlock_guard guard(wait.lock);
            wait.error = result;
            wait.done  = true;
            if (wait.self && wait.self->state() == thread_state::blocked)
                (void) scheduler::wake(wait.self.get());
            co_return astia::error_t::enone;
        }
    } // namespace

    i64 run_task(core::task<i64> operation) {
        if (!operation.valid()) {
            const error_t error = operation.error();
            return error == error_t::enone ? -ENOMEM : -errno_value(error);
        }

        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;

        auto owner_ref = retain_process(owner->pid());
        auto self_ref  = owner->retain_thread(self);
        if (!owner_ref || !self_ref)
            return -ESRCH;

        core::spinlock lock;
        volatile bool  done = false;

        if (self_ref->cancellation_pending())
            return -EINTR;

        auto waker = wake_when_done(static_cast<core::task<i64>&&>(operation), done, lock,
                                    self_ref);
        if (!waker.valid())
            return -ENOMEM;
        waker.resume();
        if (!waker.done()) {
            if (!wait_park(done, lock, owner_ref, self_ref))
                return self_ref->cancellation_pending() ? -EINTR : -ESRCH;
            // The waker completes the wait before it reaches its final
            // suspend; wait for that before destroying the coroutine frames.
            while (!waker.done())
                scheduler::yield();
        }
        return *waker.value();
    }

    i64 run_boot(core::task<void> operation) {
        if (!operation.valid()) {
            const error_t error = operation.error();
            return error == error_t::enone ? -ENOMEM : -errno_value(error);
        }

        boot_wait wait;
        auto      waker =
            wake_boot_when_done(static_cast<core::task<void>&&>(operation), wait);
        if (!waker.valid())
            return -ENOMEM;

        // The boot body runs early init and scheduler setup synchronously;
        // only its first co_await suspends.  That suspension cannot happen
        // before the process and scheduler layers are up.
        waker.resume();
        if (waker.done()) {
            arch::interrupt_spinlock_guard guard(wait.lock);
            return wait.error == error_t::enone ? 0 : -errno_value(wait.error);
        }

        auto* owner = current_process();
        auto* self  = current_thread();
        if (owner == nullptr || self == nullptr)
            return -ESRCH;

        auto owner_ref = retain_process(owner->pid());
        auto self_ref  = owner->retain_thread(self);
        if (!owner_ref || !self_ref)
            return -ESRCH;

        {
            arch::interrupt_spinlock_guard guard(wait.lock);
            wait.self = self_ref;
        }

        if (!wait_park(wait.done, wait.lock, owner_ref, self_ref))
            return -ESRCH;
        while (!waker.done())
            scheduler::yield();
        arch::interrupt_spinlock_guard guard(wait.lock);
        return wait.error == error_t::enone ? 0 : -errno_value(wait.error);
    }

} // namespace astia::proc
