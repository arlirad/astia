#pragma once

#include <astia/common.hpp>

#if defined(__x86_64__) || defined(__i386__)
#define ASTIA_CPU_RELAX() __builtin_ia32_pause()
#else
#define ASTIA_CPU_RELAX() ((void) 0)
#endif

#if defined(__SANITIZE_THREAD__)
extern "C" void __tsan_acquire(void const*);
extern "C" void __tsan_release(void const*);
#define ASTIA_TSAN_ACQUIRE(value) __tsan_acquire(value)
#define ASTIA_TSAN_RELEASE(value) __tsan_release(value)
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
extern "C" void __tsan_acquire(void const*);
extern "C" void __tsan_release(void const*);
#define ASTIA_TSAN_ACQUIRE(value) __tsan_acquire(value)
#define ASTIA_TSAN_RELEASE(value) __tsan_release(value)
#else
#define ASTIA_TSAN_ACQUIRE(value) ((void) 0)
#define ASTIA_TSAN_RELEASE(value) ((void) 0)
#endif
#else
#define ASTIA_TSAN_ACQUIRE(value) ((void) 0)
#define ASTIA_TSAN_RELEASE(value) ((void) 0)
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
            while (__atomic_exchange_n(&_flag, true, __ATOMIC_ACQUIRE)) {
                ASTIA_CPU_RELAX();
            }
            ASTIA_TSAN_ACQUIRE(&_flag);
        }

        void unlock() {
            ASTIA_TSAN_RELEASE(&_flag);
            __atomic_store_n(&_flag, false, __ATOMIC_RELEASE);
        }

        private:
        bool _flag = false;
    };

    /// Scoped (RAII) lock guard.
    class spinlock_guard {
        public:
        explicit spinlock_guard(spinlock& lock) : _lock(&lock) {
            _lock->lock();
        }
        ~spinlock_guard() {
            if (_lock != nullptr)
                _lock->unlock();
        }

        spinlock_guard(const spinlock_guard&)            = delete;
        spinlock_guard& operator=(const spinlock_guard&) = delete;

        void unlock() {
            if (_lock != nullptr) {
                _lock->unlock();
                _lock = nullptr;
            }
        }

        private:
        spinlock* _lock;
    };

} // namespace astia::core

#undef ASTIA_TSAN_ACQUIRE
#undef ASTIA_TSAN_RELEASE
