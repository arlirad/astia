#include "exports.hpp"
#include "test.hpp"

ASTIA_TEST(kernel_symbols_reject_missing_names) {
    ASTIA_CHECK(astia::mod::resolve("_ZN14kernel_fixture4biasEv") ==
                0x123456789abcdef0ull);
    ASTIA_CHECK(astia::mod::resolve(nullptr) == 0);
    ASTIA_CHECK(astia::mod::resolve("sym_missing") == 0);
    ASTIA_CHECK(astia::mod::resolve("_ZN14kernel_fixture3localEv") == 0);
}
