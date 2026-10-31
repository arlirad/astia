#pragma once

#include <astia/common.hpp>

#if defined(__x86_64__) || defined(__i386__)
#define ASTIA_CPU_RELAX() __builtin_ia32_pause()
#else
#define ASTIA_CPU_RELAX() ((void) 0)
#endif

namespace astia::core {

    /// Minimal test-and-set spinlock.
    ///
    /// Frames are handed out from every CPU once SMP comes up, so the
    /// allocator is serialised with one of these. It does *not* touch
    /// interrupts: callers that may race with an interrupt handler taking
    /// the same lock must mask interrupts around it — arch::interrupt_guard
    /// does that scoped (mask before locking, restore after unlocking).
    class spinlock {
        public:
        spinlock()                           = default;
        spinlock(const spinlock&)            = delete;
        spinlock& operator=(const spinlock&) = delete;

        void lock() {
            while (__atomic_test_and_set(&flag_, __ATOMIC_ACQUIRE)) {
                ASTIA_CPU_RELAX();
            }
        }

        void unlock() {
            __atomic_clear(&flag_, __ATOMIC_RELEASE);
        }

        private:
        bool flag_ = false;
    };

    /// Scoped (RAII) lock guard.
    class spinlock_guard {
        public:
        explicit spinlock_guard(spinlock& lock) : lock_(&lock) {
            lock_->lock();
        }
        ~spinlock_guard() {
            if (lock_ != nullptr)
                lock_->unlock();
        }

        spinlock_guard(const spinlock_guard&)            = delete;
        spinlock_guard& operator=(const spinlock_guard&) = delete;

        void unlock() {
            if (lock_ != nullptr) {
                lock_->unlock();
                lock_ = nullptr;
            }
        }

        private:
        spinlock* lock_;
    };

} // namespace astia::core
