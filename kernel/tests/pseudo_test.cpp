#include <astia/api/errno.h>
#include <astia/crypto/csprng.hpp>

#include "chr/pseudo.hpp"
#include "support/test.hpp"

#include <cstring>

ASTIA_TEST(dev_null_and_zero_semantics) {
    astia::dev::null_device null;
    astia::dev::zero_device zero;

    char       buffer[4] = {'x', 'x', 'x', 'x'};
    const char input[]   = "discard";

    ASTIA_CHECK(null.read(buffer, sizeof buffer, 0) == 0);
    ASTIA_CHECK(null.write(input, sizeof input - 1, 0) == sizeof input - 1);
    ASTIA_CHECK(zero.read(buffer, sizeof buffer, 0) == sizeof buffer);
    ASTIA_CHECK(buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 0);
    ASTIA_CHECK(zero.write(input, sizeof input - 1, 0) == sizeof input - 1);
}

ASTIA_TEST(dev_urandom_fills_and_bounds) {
    astia::dev::urandom_device urandom;

    char first[64]  = {};
    char second[64] = {};

    ASTIA_CHECK(urandom.read(first, sizeof first, 0) == sizeof first);
    ASTIA_CHECK(urandom.read(second, sizeof second, 0) == sizeof second);
    ASTIA_CHECK(std::memcmp(first, second, sizeof first) != 0);

    ASTIA_CHECK(urandom.read(nullptr, 0, 0) == 0);
    ASTIA_CHECK(urandom.read(nullptr, 4, 0) == -EFAULT);
    ASTIA_CHECK(urandom.read(first, 1, 0) == 1);
}

ASTIA_TEST(dev_urandom_chunks_large_reads) {
    astia::dev::urandom_device urandom;

    constexpr astia::u32 chunk = astia::crypto::csprng::max_request;

    std::vector<astia::u8> first(static_cast<astia::usz>(chunk) * 2);
    std::vector<astia::u8> second(static_cast<astia::usz>(chunk) * 2);

    ASTIA_CHECK(urandom.read(first.data(), chunk, 0) == static_cast<long>(chunk));
    ASTIA_CHECK(urandom.read(first.data(), chunk + 1, 0) == static_cast<long>(chunk + 1));

    ASTIA_CHECK(urandom.read(first.data(), static_cast<astia::u32>(first.size()), 0) ==
                static_cast<long>(first.size()));
    ASTIA_CHECK(urandom.read(second.data(), static_cast<astia::u32>(second.size()), 0) ==
                static_cast<long>(second.size()));

    ASTIA_CHECK(std::memcmp(first.data(), second.data(), first.size()) != 0);
}
