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
                {
                    arch::interrupt_spinlock_guard guard(lock);
                    if (done)
                        return true;
                    if (!wait_live(owner, self))
                        return false;
                    self->state(thread_state::blocked);
                }
                // A completer may run between the unlock and the yield; it sees
                // the blocked state under the same lock and wakes this thread.
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

        auto waker = wake_when_done(static_cast<core::task<i64>&&>(operation), done, lock,
                                    self_ref);
        if (!waker.valid())
            return -ENOMEM;
        waker.resume();
        if (!waker.done()) {
            if (!wait_park(done, lock, owner_ref, self_ref))
                return -ESRCH;
            // The waker completes the wait before it reaches its final
            // suspend; wait for that before destroying the coroutine frames.
            while (!waker.done())
                scheduler::yield();
        }
        return *waker.value();
    }

} // namespace astia::proc
