#include "support/test.hpp"
#include "time.hpp"

namespace {
    astia::u32 timer_frequency = 0;
    astia::u64 timer_count     = 0;
}

namespace astia::arch {
    u32 timer_hz() {
        return timer_frequency;
    }

    u64 timer_ticks() {
        return timer_count;
    }
}

ASTIA_TEST(realtime_now_rejects_unavailable_timer) {
    timer_frequency = 0;
    timer_count     = 1234;

    astia::u64 sec  = 7;
    astia::u64 nsec = 11;

    ASTIA_CHECK(!astia::sys::realtime_now(sec, nsec));
    ASTIA_CHECK(sec == 7);
    ASTIA_CHECK(nsec == 11);
}

ASTIA_TEST(realtime_now_converts_ticks) {
    timer_frequency = 100;
    timer_count     = 1234;

    astia::u64 sec  = 0;
    astia::u64 nsec = 0;

    ASTIA_CHECK(astia::sys::realtime_now(sec, nsec));
    ASTIA_CHECK(sec == 1767225612);
    ASTIA_CHECK(nsec == 340000000);
}
