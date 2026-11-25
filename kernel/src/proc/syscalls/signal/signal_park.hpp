#pragma once

#include <astia/arch/cpu.hpp>
#include <astia/core/task.hpp>

#include "../../proc.hpp"

namespace astia::proc::syscalls {

    /// Parks a signal waiter.  The caller holds an interrupt guard and has
    /// already registered with its process; registration, the pending-wake
    /// check, and arming the continuation all happen under the signal-waiter
    /// lock, so a delivery cannot slip in before the thread parks.
    class signal_park {
        public:
        signal_park(process& owner, thread& self, arch::interrupt_guard& gate)
            : _owner(owner), _self(self), _gate(gate) {}

        bool await_ready() const noexcept {
            return false;
        }
        bool await_suspend(std::coroutine_handle<> continuation) {
            const bool parked = _owner.arm_signal_waiter(&_self, continuation);
            _gate.unlock();
            return parked;
        }
        void await_resume() const noexcept {}

        private:
        process&               _owner;
        thread&                _self;
        arch::interrupt_guard& _gate;
    };

} // namespace astia::proc::syscalls
