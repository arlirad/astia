#include <astia/arch/page_map.hpp>

#include "page_map_policy.hpp"
#include "test.hpp"

namespace {

    using astia::arch::page_flags;
    using astia::arch::page_map;
    namespace policy = astia::arch::aarch64::page_map_policy;

    constexpr page_flags normal_rw    = page_flags::present | page_flags::writable;
    constexpr page_flags normal_rw_nx = normal_rw | page_flags::no_execute;
    constexpr page_flags device_rw    = normal_rw | page_flags::device;

    constexpr astia::u64 attr_index(astia::u64 bits) {
        return (bits >> 2) & 0x7;
    }

} // namespace

ASTIA_TEST(aarch64_policy_ram_defaults_to_normal_wbwa) {
    const astia::u64 bits = policy::leaf_attributes(normal_rw_nx, true);

    ASTIA_CHECK(policy::mair_device_ngnre == 0x04);
    ASTIA_CHECK(policy::mair_normal_wbwa == 0xff);
    ASTIA_CHECK(policy::mair_value == 0x00000000aa44ff04ULL);
    ASTIA_CHECK(attr_index(bits) == 1);
    ASTIA_CHECK((bits & policy::descriptor_valid) != 0);
    ASTIA_CHECK((bits & policy::descriptor_af) != 0);
    ASTIA_CHECK((bits & policy::descriptor_ap_ro) == 0);
    ASTIA_CHECK((bits & policy::descriptor_pxn) != 0);
    ASTIA_CHECK((bits & policy::descriptor_uxn) != 0);
    ASTIA_CHECK((bits & policy::descriptor_sh_inner) == policy::descriptor_sh_inner);
}

ASTIA_TEST(aarch64_policy_pl011_is_device_ngnre_and_non_executable) {
    const astia::u64 bits = policy::leaf_attributes(device_rw, true);

    ASTIA_CHECK(attr_index(bits) == 0);
    ASTIA_CHECK((bits & policy::descriptor_valid) != 0);
    ASTIA_CHECK((bits & policy::descriptor_af) != 0);
    ASTIA_CHECK((bits & policy::descriptor_ap_ro) == 0);
    ASTIA_CHECK((bits & policy::descriptor_pxn) != 0);
    ASTIA_CHECK((bits & policy::descriptor_uxn) != 0);
    ASTIA_CHECK((bits & policy::descriptor_sh_inner) == 0);
}

ASTIA_TEST(aarch64_policy_execute_permission_and_cache_flags) {
    const astia::u64 executable = policy::leaf_attributes(normal_rw, true);
    ASTIA_CHECK((executable & policy::descriptor_pxn) == 0);
    ASTIA_CHECK((executable & policy::descriptor_uxn) != 0);

    const astia::u64 read_only = policy::leaf_attributes(page_flags::present, true);
    ASTIA_CHECK((read_only & policy::descriptor_ap_ro) != 0);

    const astia::u64 non_cacheable =
        policy::leaf_attributes(normal_rw | page_flags::cache_disable, true);
    ASTIA_CHECK(attr_index(non_cacheable) == 2);

    const astia::u64 write_through =
        policy::leaf_attributes(normal_rw | page_flags::write_through, true);
    ASTIA_CHECK(attr_index(write_through) == 3);
}

ASTIA_TEST(aarch64_page_map_identity_round_trip) {
    auto map = page_map::create();
    ASTIA_CHECK(map.valid());

    constexpr astia::u64 ram   = 0x40000000ULL;
    constexpr astia::u64 code  = 0x40400000ULL;
    constexpr astia::u64 pl011 = 0x09000000ULL;

    ASTIA_CHECK(map.map_2m(ram, ram, normal_rw_nx));
    ASTIA_CHECK(map.map_4k(code, code, normal_rw));
    ASTIA_CHECK(map.map_4k(pl011, pl011, device_rw));

    ASTIA_CHECK(map.translate(ram) == ram);
    ASTIA_CHECK(map.translate(code) == code);
    ASTIA_CHECK(map.translate(pl011) == pl011);
    ASTIA_CHECK(map.writable(pl011));
    ASTIA_CHECK(!map.executable(pl011));
    ASTIA_CHECK(map.executable(code));
    ASTIA_CHECK(!map.user_accessible(code));
}

ASTIA_TEST(aarch64_page_map_rejects_alignment_and_bounds) {
    auto map = page_map::create();
    ASTIA_CHECK(map.valid());

    ASTIA_CHECK(!map.map_4k(0x1001, 0x2000, normal_rw));
    ASTIA_CHECK(!map.map_4k(0x2000, 0x1001, normal_rw));
    ASTIA_CHECK(!map.map_4k(1ULL << 48, 0x1000, normal_rw));
    ASTIA_CHECK(!map.map_4k(0x1000, 1ULL << 40, normal_rw));
    ASTIA_CHECK(!map.map_2m(0x1000, 0x200000, normal_rw));
    ASTIA_CHECK(!map.map_2m(0x200000, 0x1000, normal_rw));
}

ASTIA_TEST(aarch64_page_map_4k_split_and_unmap) {
    auto map = page_map::create();
    ASTIA_CHECK(map.valid());

    constexpr astia::u64 base = 0x600000ULL;
    ASTIA_CHECK(map.map_2m(base, base, normal_rw_nx));
    ASTIA_CHECK(map.map_4k(base + 0x1000, 0x800000, normal_rw));

    ASTIA_CHECK(map.translate(base) == base);
    ASTIA_CHECK(map.translate(base + 0x1000) == 0x800000);
    ASTIA_CHECK(map.translate(base + 0x2000) == base + 0x2000);

    ASTIA_CHECK(map.unmap(base + 0x1000));
    ASTIA_CHECK(map.translate(base + 0x1000) == page_map::invalid);
    ASTIA_CHECK(!map.unmap(base + 0x1000));
}

ASTIA_TEST(aarch64_page_map_arena_is_bounded) {
    astia::usize created = 0;
    for (; created <= policy::table_capacity; ++created) {
        auto map = page_map::create();
        if (!map.valid()) {
            break;
        }
    }

    ASTIA_CHECK(created <= policy::table_capacity);
    ASTIA_CHECK(!page_map::create().valid());
}
