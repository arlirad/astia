#include "support/test.hpp"
#include <astia/arch/platform.hpp>

ASTIA_TEST(platform_cpu_count_clamps_at_boundaries) {
    constexpr astia::u32 maximum = astia::arch::platform::max_cpu_count;
    ASTIA_CHECK(maximum == 256);
    ASTIA_CHECK(astia::arch::platform::clamp_cpu_count(0) == 1);
    ASTIA_CHECK(astia::arch::platform::clamp_cpu_count(1) == 1);
    ASTIA_CHECK(astia::arch::platform::clamp_cpu_count(maximum) == maximum);
    ASTIA_CHECK(astia::arch::platform::clamp_cpu_count(static_cast<astia::u64>(maximum) +
                                                       1) == maximum);
    ASTIA_CHECK(astia::arch::platform::clamp_cpu_count(~astia::u64{0}) == maximum);
}
