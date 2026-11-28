#include <astia/arch/entropy.hpp>
#include <astia/arch/timer.hpp>

namespace astia::arch {
    namespace {
        u64 counter = 0;

        u64 next() {
            ++counter;

            return counter * 0x9e3779b97f4a7c15ull;
        }
    } // namespace

    // The AArch64 shape from issue #526: no hardware RNG, no wall clock, and
    // a cycle counter that keeps changing.  The counter must not be enough.
    u32 hardware_random(u64*, u32) {
        return 0;
    }

    u64 cycle_counter() {
        return next();
    }

    bool wall_clock(u64&) {
        return false;
    }

    u64 timer_ticks() {
        return counter;
    }

    u32 timer_hz() {
        return 1000;
    }
} // namespace astia::arch
