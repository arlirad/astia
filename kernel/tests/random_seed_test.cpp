#include <astia/arch/entropy.hpp>
#include <astia/crypto/random.hpp>

#include "test.hpp"

#include <cstring>

ASTIA_TEST(random_requires_a_strong_source) {
    astia::u64 words[4]{};
    astia::u64 stamp = 0;

    ASTIA_CHECK(astia::arch::hardware_random(words, 4) == 0);
    ASTIA_CHECK(!astia::arch::wall_clock(stamp));

    const astia::u64 before = astia::arch::cycle_counter();
    const astia::u64 after  = astia::arch::cycle_counter();

    ASTIA_CHECK(before != 0);
    ASTIA_CHECK(before != after);

    astia::u8 output[32];

    std::memset(output, 0xa5, sizeof output);

    for (astia::u32 attempt = 0; attempt < 4; ++attempt) {
        ASTIA_CHECK(!astia::crypto::seeded());
        ASTIA_CHECK(!astia::crypto::random_bytes(output, sizeof output));
        ASTIA_CHECK(!astia::crypto::seeded());
    }
}
