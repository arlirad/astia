#include "exports.hpp"

#include "test.hpp"

// The mod export table: which kernel symbols a loadable module may bind
// against. Pure bookkeeping (no frame-allocator dependency), so it is
// host-testable on its own. The ELF loader itself is tested in elf_test.cpp.

ASTIA_TEST(exports_round_trip) {
    ASTIA_CHECK(astia::mod::expose("sym_a", 0x1000));
    ASTIA_CHECK(astia::mod::resolve("sym_a") == 0x1000);
    // Duplicate expose of the same name is rejected.
    ASTIA_CHECK(!astia::mod::expose("sym_a", 0x2000));
    // Unknown names resolve to 0.
    ASTIA_CHECK(astia::mod::resolve("sym_missing") == 0);
    // Null / zero inputs are rejected.
    ASTIA_CHECK(!astia::mod::expose(nullptr, 0x3000));
    ASTIA_CHECK(!astia::mod::expose("sym_null", 0));
}
