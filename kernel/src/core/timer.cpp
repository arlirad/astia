#include <astia/arch/spinlock_guard.hpp>
#include <astia/arch/timer.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/core/timer.hpp>

namespace astia::core {
    namespace {
        constexpr u32 max_timers = 32;

        struct timer_entry {
            void* owner             = nullptr;
            void (*callback)(void*) = nullptr;
            u64  deadline           = 0;
            bool used               = false;
        };

        timer_entry    timers[max_timers]{};
        core::spinlock timer_lock;
    } // namespace

    bool detail::arm_timer_callback(void* owner, void (*callback)(void*), u64 ticks) {
        if (owner == nullptr || callback == nullptr || ticks == 0)
            return false;

        if (arch::timer_hz() == 0)
            return false;

        arch::interrupt_spinlock_guard guard(timer_lock);

        for (timer_entry& entry : timers) {
            if (entry.used)
                continue;

            entry.owner    = owner;
            entry.callback = callback;
            entry.deadline = arch::timer_ticks() + ticks;
            entry.used     = true;

            return true;
        }

        return false;
    }

    bool detail::disarm_timer(void* owner) {
        if (owner == nullptr)
            return false;

        arch::interrupt_spinlock_guard guard(timer_lock);

        for (timer_entry& entry : timers) {
            if (!entry.used || entry.owner != owner)
                continue;

            entry = {};

            return true;
        }

        return false;
    }

    void run_timers(u64 now) {
        void (*callbacks[max_timers])(void*) = {};
        void* owners[max_timers]             = {};

        u32 count = 0;

        {
            arch::interrupt_spinlock_guard guard(timer_lock);

            for (timer_entry& entry : timers) {
                if (!entry.used || entry.deadline > now)
                    continue;

                callbacks[count] = entry.callback;
                owners[count]    = entry.owner;

                ++count;

                entry = {};
            }
        }

        for (u32 index = 0; index < count; ++index) {
            callbacks[index](owners[index]);
        }
    }
} // namespace astia::core
