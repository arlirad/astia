#include "chr/pseudo.hpp"
#include "support/test.hpp"

ASTIA_TEST(dev_null_and_zero_semantics) {
    astia::dev::null_device null;
    astia::dev::zero_device zero;
    char                    buffer[4] = {'x', 'x', 'x', 'x'};
    const char              input[]   = "discard";
    ASTIA_CHECK(null.read(buffer, sizeof buffer, 0) == 0);
    ASTIA_CHECK(null.write(input, sizeof input - 1, 0) == sizeof input - 1);
    ASTIA_CHECK(zero.read(buffer, sizeof buffer, 0) == sizeof buffer);
    ASTIA_CHECK(buffer[0] == 0 && buffer[1] == 0 && buffer[2] == 0 && buffer[3] == 0);
    ASTIA_CHECK(zero.write(input, sizeof input - 1, 0) == sizeof input - 1);
}
