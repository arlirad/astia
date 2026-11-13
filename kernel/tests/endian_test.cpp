#include "support/test.hpp"

#include <astia/core/endian.hpp>

ASTIA_TEST(endian_wire_encoding_and_decoding) {
    astia::u8 little[4]{};
    astia::u8 big[4]{};
    astia::core::le<astia::u32>(0x12345678u).store(little);
    astia::core::be<astia::u32>(0x12345678u).store(big);
    ASTIA_CHECK(little[0] == 0x78 && little[1] == 0x56 && little[2] == 0x34 &&
                little[3] == 0x12);
    ASTIA_CHECK(big[0] == 0x12 && big[1] == 0x34 && big[2] == 0x56 && big[3] == 0x78);
    ASTIA_CHECK(astia::core::le<astia::u32>::load(little) == 0x12345678u);
    ASTIA_CHECK(astia::core::be<astia::u32>::load(big) == 0x12345678u);
}

ASTIA_TEST(endian_unaligned_wire_load) {
    const astia::u8 bytes[] = {0xff, 0x78, 0x56, 0x34, 0x12, 0xee};
    ASTIA_CHECK(astia::core::le<astia::u32>::load(bytes + 1) == 0x12345678u);
}
