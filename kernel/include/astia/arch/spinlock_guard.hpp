#pragma once

#include <astia/arch/cpu.hpp>
#include <astia/arch/spinlock_guard.hpp>
#include <astia/core/spinlock.hpp>

namespace astia::arch {

    /// Mask interrupts before acquiring a lock so an IRQ on this CPU cannot
    /// preempt a holder and spin forever on that same lock.
    class interrupt_spinlock_guard {
        public:
        explicit interrupt_spinlock_guard(core::spinlock& lock) : _gate(), _lock(lock) {
            _lock.lock();
        }
        ~interrupt_spinlock_guard() {
            unlock();
        }

        interrupt_spinlock_guard(const interrupt_spinlock_guard&)            = delete;
        interrupt_spinlock_guard& operator=(const interrupt_spinlock_guard&) = delete;

        void unlock() {
            if (_locked) {
                _lock.unlock();
                _locked = false;
                _gate.unlock();
            }
        }

        private:
        interrupt_guard _gate;
        core::spinlock& _lock;
        bool            _locked = true;
    };

} // namespace astia::arch
