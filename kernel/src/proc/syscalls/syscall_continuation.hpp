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

        arch::syscall_frame _frame{};
        mm::shptr<process> _owner;
        mm::shptr<thread> _waiter;
        i64 _result = -EINTR;
        u8  _phase  = static_cast<u8>(phase::pending);

        phase current_phase() const {
            return static_cast<phase>(__atomic_load_n(&_phase, __ATOMIC_ACQUIRE));
        }

        bool live() const {
            if (!_owner || !_waiter || _waiter->state() == thread_state::dead)
                return false;
            const process_state state = _owner->state();
            return state != process_state::zombie && state != process_state::dead;
        }

        void wake_waiter() {
            if (!live())
                return;
            if (current_thread() == _waiter.get()) {
                _waiter->state(thread_state::running);
                return;
            }
            (void) scheduler::wake(_waiter.get());
        }

        bool arm() {
            u8 expected = static_cast<u8>(phase::pending);
            if (__atomic_compare_exchange_n(&_phase, &expected,
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
                if (__atomic_compare_exchange_n(&_phase, &expected,
                                                static_cast<u8>(phase::completing), false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    break;
            }
            _result = value;
            __atomic_store_n(&_phase, static_cast<u8>(target), __ATOMIC_RELEASE);
            if (was_parked && wake)
                wake_waiter();
            return true;
        }

        public:
        syscall_continuation(const arch::syscall_frame& frame,
                             const mm::shptr<process>& owner = {},
                             const mm::shptr<thread>& waiter = {})
            : _frame(frame), _owner(owner), _waiter(waiter) {}

        ~syscall_continuation() {
            (void) finish(phase::canceled, -EINTR, false);
        }

        syscall_continuation(const syscall_continuation&) = delete;
        syscall_continuation& operator=(const syscall_continuation&) = delete;

        void bind(const mm::shptr<process>& owner, const mm::shptr<thread>& waiter) {
            _owner  = owner;
            _waiter = waiter;
        }

        const arch::syscall_frame& frame() const {
            return _frame;
        }

        process* owner() const {
            return _owner.get();
        }

        thread* waiter() const {
            return _waiter.get();
        }

        i64 result() const {
            return _result;
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

                _waiter->state(thread_state::blocked);
                if (!arm()) {
                    _waiter->state(thread_state::running);
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
