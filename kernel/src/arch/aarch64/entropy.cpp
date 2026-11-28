#include <astia/arch/entropy.hpp>

namespace astia::arch {
    // ARMv8.5 RNDR is optional and this port does not probe or use it.  CNTPCT
    // and the timer are mixed as weak material only; with no strong source
    // (a future RNDR path, a loader seed, or an RTC driver) the CSPRNG stays
    // unseeded and callers get EAGAIN instead of predictable bytes.
    u32 hardware_random(u64*, u32) {
        return 0;
    }

    u64 cycle_counter() {
        u64 value = 0;

        asm volatile("mrs %0, cntpct_el0" : "=r"(value));

        return value;
    }

    bool wall_clock(u64&) {
        return false;
    }
} // namespace astia::arch
