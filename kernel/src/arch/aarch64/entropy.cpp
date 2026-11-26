#include <astia/arch/entropy.hpp>

namespace astia::arch {
    // ARMv8.5 RNDR is optional and this port does not probe or use it.  The
    // generic jitter path seeds the DRBG from CNTPCT until the platform
    // grows a TRNG or RTC driver.
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
