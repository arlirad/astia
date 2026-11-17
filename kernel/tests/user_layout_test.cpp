#include "test.hpp"
#include <astia/proc/user_layout.hpp>

using astia::proc::is_user_address_canonical;
using astia::proc::is_user_address_range;
using astia::proc::user_address_limit;
using astia::proc::user_image_base;

ASTIA_TEST(user_layout_canonical_addresses) {
    ASTIA_CHECK(is_user_address_canonical(0));
    ASTIA_CHECK(is_user_address_canonical(user_address_limit - 1));
    ASTIA_CHECK(is_user_address_canonical(0xffff800000000000ull));
    ASTIA_CHECK(is_user_address_canonical(0xffffffffffffffffull));
    ASTIA_CHECK(!is_user_address_canonical(0x0000800000000000ull));
    ASTIA_CHECK(!is_user_address_canonical(0x0001000000000000ull));
    ASTIA_CHECK(!is_user_address_canonical(0xffff000000000000ull));
}

ASTIA_TEST(user_layout_user_address_ranges) {
    ASTIA_CHECK(is_user_address_range(user_image_base, astia::mm::page_size));
    ASTIA_CHECK(is_user_address_range(user_address_limit - 1));
    ASTIA_CHECK(!is_user_address_range(user_address_limit));
    ASTIA_CHECK(!is_user_address_range(user_address_limit - 1, 2));
    ASTIA_CHECK(!is_user_address_range(0x0001000000000000ull));
    ASTIA_CHECK(!is_user_address_range(~astia::u64{}, 2));
    ASTIA_CHECK(!is_user_address_range(user_image_base, 0));
}
