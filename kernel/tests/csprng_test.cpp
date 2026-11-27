#include <astia/crypto/csprng.hpp>

#include "test.hpp"

#include <cstring>

namespace {
    using astia::crypto::csprng;

    void fill(astia::crypto::csprng& generator, astia::u8* output, astia::usz length) {
        ASTIA_CHECK(generator.fill(output, length));
    }

    void seed(astia::crypto::csprng& generator, astia::u8 value) {
        astia::u8 material[40];

        for (astia::usz i = 0; i < sizeof material; ++i) {
            material[i] = static_cast<astia::u8>(value + i * 7u);
        }

        generator.reseed(material, sizeof material);
        ASTIA_CHECK(generator.seeded());
    }
} // namespace

ASTIA_TEST(csprng_chacha20_rfc8439_block) {
    astia::u8 key[32];
    astia::u8 nonce[12] = {0, 0, 0, 9, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    astia::u8 output[64];

    for (astia::u32 i = 0; i < sizeof key; ++i) {
        key[i] = static_cast<astia::u8>(i);
    }

    csprng::chacha20_block(key, nonce, 1, output);

    const astia::u8 expected[64] = {
        0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3,
        0x20, 0x71, 0xc4, 0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22,
        0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e, 0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa,
        0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2, 0xb5, 0x12, 0x9c, 0xd1,
        0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e};

    ASTIA_CHECK(std::memcmp(output, expected, sizeof output) == 0);
}

ASTIA_TEST(csprng_refuses_unseeded_output) {
    csprng    generator;
    astia::u8 output[16];

    std::memset(output, 0xa5, sizeof output);

    ASTIA_CHECK(!generator.seeded());
    ASTIA_CHECK(!generator.fill(output, sizeof output));
    ASTIA_CHECK(!generator.seeded());

    for (astia::u8 byte : output) {
        ASTIA_CHECK(byte == 0);
    }
}

ASTIA_TEST(csprng_same_seed_same_stream) {
    csprng    first;
    csprng    second;
    astia::u8 a[100]{};
    astia::u8 b[100]{};

    seed(first, 0x11);
    seed(second, 0x11);

    fill(first, a, sizeof a);
    fill(second, b, sizeof b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) == 0);
}

ASTIA_TEST(csprng_different_seed_different_stream) {
    csprng    first;
    csprng    second;
    astia::u8 a[64]{};
    astia::u8 b[64]{};

    seed(first, 0x11);
    seed(second, 0x12);

    fill(first, a, sizeof a);
    fill(second, b, sizeof b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) != 0);
}

ASTIA_TEST(csprng_fill_is_a_prefix_of_one_stream) {
    csprng    split;
    csprng    whole;
    astia::u8 a[128]{};
    astia::u8 b[128]{};

    seed(split, 0x33);
    seed(whole, 0x33);

    fill(split, a, 100);
    fill(split, a + 100, 28);
    fill(whole, b, sizeof b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) == 0);
}

ASTIA_TEST(csprng_stir_changes_the_stream) {
    csprng          untouched;
    csprng          stirred;
    astia::u8       a[64]{};
    astia::u8       b[64]{};
    const astia::u8 material[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    seed(untouched, 0x44);
    seed(stirred, 0x44);

    stirred.stir(material, sizeof material);

    fill(untouched, a, sizeof a);
    fill(stirred, b, sizeof b);

    ASTIA_CHECK(std::memcmp(a, b, sizeof a) != 0);
}

ASTIA_TEST(csprng_fork_derivation_diverges) {
    csprng    parent;
    astia::u8 before[64]{};
    astia::u8 after[64]{};

    seed(parent, 0x55);
    fill(parent, before, sizeof before);

    parent.fork_derive(42);
    fill(parent, after, sizeof after);

    ASTIA_CHECK(std::memcmp(before, after, sizeof before) != 0);
}

ASTIA_TEST(csprng_reseed_replaces_the_stream) {
    csprng    generator;
    astia::u8 first[64]{};
    astia::u8 second[64]{};

    seed(generator, 0x66);
    fill(generator, first, sizeof first);

    seed(generator, 0x67);
    fill(generator, second, sizeof second);

    ASTIA_CHECK(std::memcmp(first, second, sizeof first) != 0);
}
