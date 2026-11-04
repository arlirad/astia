#pragma once

#include "../proc.hpp"
#include "../scheduler.hpp"
#include <astia/api/errno.h>
#include <astia/arch/context.hpp>

namespace astia::proc::syscall {

    class syscall_continuation {
        enum class phase : u8 {
            pending,
            parked,
            completing,
            completed,
            canceled,
        };

        arch::syscall_frame frame_{};
        mm::shptr<process> owner_;
        mm::shptr<thread> waiter_;
        i64 result_ = -EINTR;
        u8  phase_  = static_cast<u8>(phase::pending);

        phase current_phase() const {
            return static_cast<phase>(__atomic_load_n(&phase_, __ATOMIC_ACQUIRE));
        }

        bool live() const {
            if (!owner_ || !waiter_ || waiter_->state() == thread_state::dead)
                return false;
            const process_state state = owner_->state();
            return state != process_state::zombie && state != process_state::dead;
        }

        void wake_waiter() {
            if (!live())
                return;
            if (current_thread() == waiter_.get()) {
                waiter_->state(thread_state::running);
                return;
            }
            (void) scheduler::wake(waiter_.get());
        }

        bool arm() {
            u8 expected = static_cast<u8>(phase::pending);
            if (__atomic_compare_exchange_n(&phase_, &expected,
                                            static_cast<u8>(phase::parked), false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return true;
            return static_cast<phase>(expected) == phase::parked;
        }

        bool finish(phase target, i64 value, bool wake) {
            u8 expected = static_cast<u8>(phase::pending);
            bool was_parked = false;
            for (;;) {
                const phase observed = static_cast<phase>(expected);
                if (observed == phase::parked)
                    was_parked = true;
                if (observed != phase::pending && observed != phase::parked)
                    return false;
                if (__atomic_compare_exchange_n(&phase_, &expected,
                                                static_cast<u8>(phase::completing), false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    break;
            }
            result_ = value;
            __atomic_store_n(&phase_, static_cast<u8>(target), __ATOMIC_RELEASE);
            if (was_parked && wake)
                wake_waiter();
            return true;
        }

        public:
        syscall_continuation(const arch::syscall_frame& frame,
                             const mm::shptr<process>& owner = {},
                             const mm::shptr<thread>& waiter = {})
            : frame_(frame), owner_(owner), waiter_(waiter) {}

        ~syscall_continuation() {
            (void) finish(phase::canceled, -EINTR, false);
        }

        syscall_continuation(const syscall_continuation&) = delete;
        syscall_continuation& operator=(const syscall_continuation&) = delete;

        void bind(const mm::shptr<process>& owner, const mm::shptr<thread>& waiter) {
            owner_  = owner;
            waiter_ = waiter;
        }

        const arch::syscall_frame& frame() const {
            return frame_;
        }

        process* owner() const {
            return owner_.get();
        }

        thread* waiter() const {
            return waiter_.get();
        }

        i64 result() const {
            return result_;
        }

        bool completed() const {
            return current_phase() == phase::completed;
        }

        bool complete(i64 result) {
            return finish(phase::completed, result, true);
        }

        bool wait() {
            for (;;) {
                const phase state = current_phase();
                if (state == phase::completed)
                    return true;
                if (state == phase::canceled)
                    return false;
                if (!live()) {
                    (void) finish(phase::canceled, -ESRCH, false);
                    return false;
                }

                waiter_->state(thread_state::blocked);
                if (!arm()) {
                    waiter_->state(thread_state::running);
                    continue;
                }
                if (current_phase() == phase::pending ||
                    current_phase() == phase::parked) {
                    scheduler::yield();
                }
            }
        }
    };

} // namespace astia::proc::syscall
