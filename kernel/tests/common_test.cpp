#include "support/test.hpp"
#include <astia/common.hpp>

using namespace astia::literals;

static_assert(sizeof(astia::paddr_t) == sizeof(astia::u64));
static_assert(sizeof(astia::vaddr_t) == sizeof(astia::u64));
static_assert(1_b == 1);
static_assert(4_kib == 4096);
static_assert(2_mib == 2 * 1024 * 1024);
static_assert(1_gib == 1024ull * 1024 * 1024);

ASTIA_TEST(common_type_vocabulary) {
    const astia::bytes_t  bytes           = 16_kib;
    const astia::count_t  count           = 3;
    const astia::offset_t offset          = 2_b;
    const astia::paddr_t  physical        = 0x1000;
    const astia::vaddr_t  virtual_address = 0xffff800000001000ull;

    ASTIA_CHECK(bytes == 16 * 1024);
    ASTIA_CHECK(count == 3);
    ASTIA_CHECK(offset == 2);
    ASTIA_CHECK(physical != virtual_address);
}
