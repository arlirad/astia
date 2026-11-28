#include <astia/crypto/entropy_pool.hpp>

#include "test.hpp"

#include <cstring>

namespace {
    using astia::crypto::entropy_pool;
    using astia::crypto::entropy_quality;

    constexpr astia::u64 strong_word = 0x0123456789abcdefULL;

    void populate(entropy_pool& pool) {
        pool.add_word(entropy_quality::strong, strong_word);

        for (astia::u32 i = 0; i < 8; ++i) {
            pool.add_word(entropy_quality::weak, 0x1000 + i);
        }
    }

    void seed_of(const entropy_pool& pool, astia::u8 output[entropy_pool::seed_size]) {
        ASTIA_CHECK(pool.copy(output));
    }
} // namespace

ASTIA_TEST(entropy_pool_weak_material_is_never_sufficient) {
    entropy_pool pool;

    for (astia::u32 i = 0; i < 64; ++i) {
        pool.add_word(entropy_quality::weak, 0x2000 + i);
    }

    ASTIA_CHECK(pool.weak_words() > 0);
    ASTIA_CHECK(pool.strong_words() == 0);
    ASTIA_CHECK(!pool.ready());

    astia::u8 seed[entropy_pool::seed_size];

    std::memset(seed, 0xa5, sizeof seed);
    ASTIA_CHECK(!pool.copy(seed));

    for (astia::u8 byte : seed) {
        ASTIA_CHECK(byte == 0);
    }
}

ASTIA_TEST(entropy_pool_strong_zero_is_not_entropy) {
    entropy_pool pool;

    pool.add_word(entropy_quality::strong, 0);

    for (astia::u32 i = 0; i < 32; ++i) {
        pool.add_word(entropy_quality::weak, 0x3000 + i);
    }

    ASTIA_CHECK(pool.strong_words() == 0);
    ASTIA_CHECK(!pool.ready());

    const astia::u64 zeros[4]{};

    pool.add_words(entropy_quality::strong, zeros, 4);
    ASTIA_CHECK(pool.strong_words() == 0);
    ASTIA_CHECK(!pool.ready());
}

ASTIA_TEST(entropy_pool_one_strong_word_seeds) {
    entropy_pool pool;

    ASTIA_CHECK(!pool.ready());
    pool.add_word(entropy_quality::strong, 1);

    ASTIA_CHECK(pool.strong_words() == 1);
    ASTIA_CHECK(pool.ready());

    astia::u8 seed[entropy_pool::seed_size];

    seed_of(pool, seed);
}

ASTIA_TEST(entropy_pool_strong_bytes_count_as_words) {
    entropy_pool pool;
    astia::u8    boot_seed[32];

    for (astia::u32 i = 0; i < sizeof boot_seed; ++i) {
        boot_seed[i] = static_cast<astia::u8>(i + 1);
    }

    pool.add_bytes(entropy_quality::strong, boot_seed, sizeof boot_seed);

    ASTIA_CHECK(pool.strong_words() == 4);
    ASTIA_CHECK(pool.ready());
}

ASTIA_TEST(entropy_pool_is_deterministic) {
    entropy_pool first;
    entropy_pool second;
    astia::u8    a[entropy_pool::seed_size]{};
    astia::u8    b[entropy_pool::seed_size]{};

    populate(first);
    populate(second);

    seed_of(first, a);
    seed_of(second, b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) == 0);
}

ASTIA_TEST(entropy_pool_weak_material_changes_the_seed) {
    entropy_pool base;
    entropy_pool stirred;
    astia::u8    a[entropy_pool::seed_size]{};
    astia::u8    b[entropy_pool::seed_size]{};

    populate(base);
    populate(stirred);

    stirred.add_word(entropy_quality::weak, 0xfeedface);

    seed_of(base, a);
    seed_of(stirred, b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) != 0);
}

ASTIA_TEST(entropy_pool_strong_material_dominates_identical_weak_input) {
    entropy_pool first;
    entropy_pool second;
    astia::u8    a[entropy_pool::seed_size]{};
    astia::u8    b[entropy_pool::seed_size]{};

    first.add_word(entropy_quality::strong, strong_word);
    second.add_word(entropy_quality::strong, strong_word + 1);

    for (astia::u32 i = 0; i < 16; ++i) {
        first.add_word(entropy_quality::weak, 0x4000 + i);
        second.add_word(entropy_quality::weak, 0x4000 + i);
    }

    seed_of(first, a);
    seed_of(second, b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) != 0);
}

ASTIA_TEST(entropy_pool_contribution_order_matters) {
    entropy_pool forward;
    entropy_pool reverse;
    astia::u8    a[entropy_pool::seed_size]{};
    astia::u8    b[entropy_pool::seed_size]{};

    forward.add_word(entropy_quality::strong, 0x1111111111111111ULL);
    forward.add_word(entropy_quality::strong, 0x2222222222222222ULL);

    reverse.add_word(entropy_quality::strong, 0x2222222222222222ULL);
    reverse.add_word(entropy_quality::strong, 0x1111111111111111ULL);

    seed_of(forward, a);
    seed_of(reverse, b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) != 0);
}
