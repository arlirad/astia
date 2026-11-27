#include "timer_host.hpp"

#include <astia/arch/timer.hpp>
#include <astia/core/timer.hpp>

namespace astia::test_timer {
    struct entry {
        void* owner             = nullptr;
        void (*callback)(void*) = nullptr;
        u64  deadline           = 0;
        bool used               = false;
    };

    inline constexpr u32 capacity = 32;

    entry entries[capacity]{};
    u64   now = 0;
} // namespace astia::test_timer

namespace astia::arch {
    u64 timer_ticks() {
        return test_timer::now;
    }

    u32 timer_hz() {
        return 1000;
    }
} // namespace astia::arch

namespace astia::core::detail {
    bool arm_timer_callback(void* owner, void (*callback)(void*), u64 ticks) {
        if (owner == nullptr || callback == nullptr || ticks == 0)
            return false;

        for (auto& entry : test_timer::entries) {
            if (entry.used)
                continue;

            entry.owner    = owner;
            entry.callback = callback;
            entry.deadline = test_timer::now + ticks;
            entry.used     = true;

            return true;
        }

        return false;
    }

    bool disarm_timer(void* owner) {
        if (owner == nullptr)
            return false;

        for (auto& entry : test_timer::entries) {
            if (!entry.used || entry.owner != owner)
                continue;

            entry = {};

            return true;
        }

        return false;
    }
} // namespace astia::core::detail

namespace astia::test {
    void timer_reset() {
        for (auto& entry : test_timer::entries) {
            entry = {};
        }

        test_timer::now = 0;
    }

    void timer_advance(u64 ticks) {
        test_timer::now += ticks;

        bool fired = true;

        while (fired) {
            fired = false;

            for (auto& entry : test_timer::entries) {
                if (!entry.used || entry.deadline > test_timer::now)
                    continue;

                const auto copy = entry;

                entry = {};
                fired = true;

                if (copy.callback != nullptr)
                    copy.callback(copy.owner);

                break;
            }
        }
    }
} // namespace astia::test
