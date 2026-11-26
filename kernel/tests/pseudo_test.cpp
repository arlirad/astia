#include <astia/api/errno.h>

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
    ASTIA_CHECK(urandom.read(first, 1024 * 1024 + 1, 0) == -EINVAL);
}
