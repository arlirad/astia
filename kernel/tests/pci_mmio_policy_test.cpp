#include <astia/arch/mmio.hpp>

#include "pci_mmio_policy.hpp"
#include "test.hpp"

namespace {

    using astia::arch::mmio_borrow_status;
    using astia::arch::mmio_borrow_status_for;
    using astia::arch::page_flags;
    using astia::arch::x86_64::pci_identity_window_size;
    using astia::arch::x86_64::pci_mmio_canonical_high_base;
    using astia::arch::x86_64::pci_mmio_canonical_low_limit;
    using astia::arch::x86_64::pci_mmio_page_span_of;
    using astia::arch::x86_64::pci_mmio_range;
    using astia::arch::x86_64::pci_mmio_range_check;
    using astia::arch::x86_64::pci_mmio_range_fits_direct_map;
    using astia::arch::x86_64::pci_mmio_safe_floor;
    using astia::arch::x86_64::pci_mmio_spans_overlap;
    using astia::arch::x86_64::pci_mmio_va_is_canonical;
    using astia::arch::x86_64::pci_mmio_va_limits;
    using astia::arch::x86_64::pci_mmio_va_limits_for;
    using astia::arch::x86_64::pci_mmio_va_span_is_canonical;

    bool is_strict_uncached_rw(page_flags flags) {
        return astia::arch::has_flag(flags, page_flags::present) &&
               astia::arch::has_flag(flags, page_flags::writable) &&
               astia::arch::has_flag(flags, page_flags::cache_disable) &&
               astia::arch::has_flag(flags, page_flags::write_through);
    }

} // namespace

ASTIA_TEST(pci_bar_non_prefetchable_maps_strict_uc) {
    ASTIA_CHECK(is_strict_uncached_rw(astia::arch::x86_64::pci_bar_page_flags(false)));
}

ASTIA_TEST(pci_bar_prefetchable_maps_strict_uc) {
    ASTIA_CHECK(is_strict_uncached_rw(astia::arch::x86_64::pci_bar_page_flags(true)));
}

ASTIA_TEST(pci_bar_cache_policy_ignores_prefetchable) {
    ASTIA_CHECK(astia::arch::x86_64::pci_bar_page_flags(false) ==
                astia::arch::x86_64::pci_bar_page_flags(true));
}

ASTIA_TEST(pci_bar_below_identity_floor_rejected) {
    ASTIA_CHECK(pci_mmio_range_check(0x3ff00000, 0x1000, pci_identity_window_size) ==
                pci_mmio_range::write_back_alias);
}

ASTIA_TEST(pci_bar_overlapping_direct_map_floor_rejected) {
    constexpr astia::u64 direct_map_floor = 0x1'0000'0000ull;
    ASTIA_CHECK(
        pci_mmio_range_check(direct_map_floor - 0x1000, 0x2000, direct_map_floor) ==
        pci_mmio_range::write_back_alias);
}

ASTIA_TEST(pci_bar_fresh_high_extent_accepted) {
    constexpr astia::u64 direct_map_floor = 0x1'0000'0000ull;
    ASTIA_CHECK(pci_mmio_range_check(direct_map_floor + 0x2000, 0x4000,
                                     direct_map_floor) == pci_mmio_range::accepted);
}

ASTIA_TEST(pci_bar_page_rounding_boundary_rejected) {
    ASTIA_CHECK(
        pci_mmio_range_check(pci_identity_window_size - 1, 1, pci_identity_window_size) ==
        pci_mmio_range::write_back_alias);
    ASTIA_CHECK(
        pci_mmio_range_check(pci_identity_window_size, 1, pci_identity_window_size) ==
        pci_mmio_range::accepted);
}

ASTIA_TEST(pci_bar_invalid_range_rejected) {
    ASTIA_CHECK(pci_mmio_range_check(0, 0, pci_identity_window_size) ==
                pci_mmio_range::invalid);
    ASTIA_CHECK(pci_mmio_range_check(~astia::u64{0}, 2, pci_identity_window_size) ==
                pci_mmio_range::invalid);
    ASTIA_CHECK(
        pci_mmio_range_check(~astia::u64{0} - 0x1000, 0x2000, pci_identity_window_size) ==
        pci_mmio_range::invalid);
}

ASTIA_TEST(pci_mmio_safe_floor_uses_max) {
    ASTIA_CHECK(pci_mmio_safe_floor(0x2000'0000ull) == pci_identity_window_size);
    ASTIA_CHECK(pci_mmio_safe_floor(0x1'0000'0000ull) == 0x1'0000'0000ull);
}

ASTIA_TEST(pci_bar_disjoint_bytes_in_one_page_overlap) {
    const auto first  = pci_mmio_page_span_of(0x1000, 0x100);
    const auto second = pci_mmio_page_span_of(0x1100, 0x100);
    ASTIA_CHECK(first.first == 0x1000);
    ASTIA_CHECK(first.last == 0x1000);
    ASTIA_CHECK(second.first == 0x1000);
    ASTIA_CHECK(second.last == 0x1000);
    ASTIA_CHECK(pci_mmio_spans_overlap(first, second));
}

ASTIA_TEST(pci_bar_adjacent_page_spans_do_not_overlap) {
    const auto first  = pci_mmio_page_span_of(0x1000, 0x1000);
    const auto second = pci_mmio_page_span_of(0x2000, 0x1000);
    ASTIA_CHECK(!pci_mmio_spans_overlap(first, second));
}

ASTIA_TEST(pci_bar_active_page_span_conflict) {
    const auto active    = pci_mmio_page_span_of(0x4000, 0x1000);
    const auto candidate = pci_mmio_page_span_of(0x4400, 0x100);
    ASTIA_CHECK(pci_mmio_spans_overlap(active, candidate));
}

ASTIA_TEST(mmio_borrow_status_pci_owned_wins) {
    ASTIA_CHECK(mmio_borrow_status_for(true, false, false, false) ==
                mmio_borrow_status::pci_owned);
    ASTIA_CHECK(mmio_borrow_status_for(true, true, true, true) ==
                mmio_borrow_status::pci_owned);
}

ASTIA_TEST(mmio_borrow_status_generic_owned_wins) {
    ASTIA_CHECK(mmio_borrow_status_for(false, true, false, false) ==
                mmio_borrow_status::generic_owned);
    ASTIA_CHECK(mmio_borrow_status_for(false, true, true, true) ==
                mmio_borrow_status::generic_owned);
}

ASTIA_TEST(mmio_borrow_status_direct_map_is_borrowable) {
    ASTIA_CHECK(mmio_borrow_status_for(false, false, true, true) ==
                mmio_borrow_status::borrowable);
}

ASTIA_TEST(mmio_borrow_status_existing_alias_is_not_generic_mappable) {
    ASTIA_CHECK(mmio_borrow_status_for(false, false, false, true) ==
                mmio_borrow_status::aliased);
}

ASTIA_TEST(mmio_borrow_status_without_direct_map_is_absent) {
    ASTIA_CHECK(mmio_borrow_status_for(false, false, false, false) ==
                mmio_borrow_status::absent);
}

ASTIA_TEST(pci_bar_direct_map_va_limits_are_bounded) {
    constexpr astia::u64 kernel_base      = 0xFFFF800000000000ull;
    constexpr astia::u64 direct_map_limit = 1ull << 39;
    constexpr astia::u64 mmio_base        = 0xFFFFC00000000000ull;
    constexpr astia::u64 recursive_base   = 0xFFFFFF0000000000ull;
    constexpr astia::u64 image_base       = 0xFFFFFFFF80000000ull;

    constexpr auto limits = pci_mmio_va_limits_for(kernel_base, direct_map_limit,
                                                   mmio_base, recursive_base, image_base);
    ASTIA_CHECK(limits.direct_map_limit == direct_map_limit);
    ASTIA_CHECK(limits.reserved_floor == mmio_base - kernel_base);
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(direct_map_limit - 0x2000,
                                               direct_map_limit - 0x1000, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(direct_map_limit - 0x1000,
                                                direct_map_limit, limits));
}

ASTIA_TEST(pci_bar_reserved_higher_half_rejected) {
    constexpr astia::u64 kernel_base    = 0xFFFF800000000000ull;
    constexpr astia::u64 reserved_floor = 0x400000000000ull;
    constexpr auto limits = pci_mmio_va_limits{kernel_base, 1ull << 50, reserved_floor};
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(reserved_floor - 0x2000,
                                               reserved_floor - 0x1000, limits));
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(reserved_floor - 0x1000,
                                               reserved_floor - 0x1000, limits));
    ASTIA_CHECK(
        !pci_mmio_range_fits_direct_map(reserved_floor - 0x1000, reserved_floor, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(reserved_floor, reserved_floor, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, 0, {}));
}

ASTIA_TEST(pci_bar_direct_map_boundary_pages) {
    constexpr astia::u64 kernel_base      = 0xFFFF800000000000ull;
    constexpr astia::u64 direct_map_limit = 1ull << 39;
    constexpr astia::u64 mmio_base        = 0xFFFFC00000000000ull;
    constexpr astia::u64 recursive_base   = 0xFFFFFF0000000000ull;
    constexpr astia::u64 image_base       = 0xFFFFFFFF80000000ull;

    constexpr auto limits = pci_mmio_va_limits_for(kernel_base, direct_map_limit,
                                                   mmio_base, recursive_base, image_base);

    constexpr astia::u64 last_page = direct_map_limit - 0x1000;
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(last_page, last_page, limits));
    ASTIA_CHECK(
        !pci_mmio_range_fits_direct_map(direct_map_limit, direct_map_limit, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0x1'0000'0000'0000ull,
                                                0x1'0000'0000'0000ull, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(direct_map_limit - 0x2000,
                                                direct_map_limit, limits));
}

ASTIA_TEST(pci_bar_huge_span_rejected_without_iteration) {
    constexpr astia::u64 kernel_base      = 0xFFFF800000000000ull;
    constexpr astia::u64 direct_map_limit = 1ull << 39;
    constexpr auto       limits =
        pci_mmio_va_limits{kernel_base, direct_map_limit, direct_map_limit};
    ASTIA_CHECK(
        !pci_mmio_range_fits_direct_map(0x1'0000'0000ull, 0x4000'0000'0000ull, limits));
}

ASTIA_TEST(pci_bar_unaligned_and_translate_wrap_rejected) {
    constexpr astia::u64 kernel_base = 0xFFFF800000000000ull;
    constexpr auto       limits =
        pci_mmio_va_limits{kernel_base, 0x800000001000ull, 0x800000001000ull};

    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0x1001, 0x2000, limits));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0x1000, 0x2001, limits));
    ASTIA_CHECK(
        !pci_mmio_range_fits_direct_map(0x7ffffffff000ull, 0x800000000000ull, limits));
}

ASTIA_TEST(pci_bar_canonical_span_must_not_cross_hole) {
    constexpr astia::u64 crossing_end = 0xffff800000000000ull;

    ASTIA_CHECK(pci_mmio_va_is_canonical(0));
    ASTIA_CHECK(pci_mmio_va_is_canonical(crossing_end));
    ASTIA_CHECK(!pci_mmio_va_span_is_canonical(0, crossing_end));

    ASTIA_CHECK(pci_mmio_va_span_is_canonical(0, pci_mmio_canonical_low_limit - 1));
    ASTIA_CHECK(
        pci_mmio_va_span_is_canonical(pci_mmio_canonical_high_base, ~astia::u64{0}));

    constexpr auto crossing_limits =
        pci_mmio_va_limits{0x1000, ~astia::u64{0}, ~astia::u64{0}};
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, crossing_end, crossing_limits));
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(0, 0x1000, crossing_limits));
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(
        0, 0x1000,
        pci_mmio_va_limits{pci_mmio_canonical_high_base, ~astia::u64{0},
                           ~astia::u64{0}}));
}

ASTIA_TEST(pci_bar_direct_map_va_span_must_be_canonical) {
    constexpr astia::u64 limit = 1ull << 39;
    constexpr auto       counterexample =
        pci_mmio_va_limits{0x0000800000000000ull, limit, limit};

    ASTIA_CHECK(!pci_mmio_va_is_canonical(0x0000800000000000ull));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, 0, counterexample));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, 0x1000, counterexample));

    constexpr auto accepted_boundary =
        pci_mmio_va_limits{0x00007FFFFFFFF000ull, 0x2000, 0x2000};
    ASTIA_CHECK(pci_mmio_va_is_canonical(0x00007FFFFFFFF000ull));
    ASTIA_CHECK(pci_mmio_range_fits_direct_map(0, 0, accepted_boundary));
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, 0x1000, accepted_boundary));

    constexpr auto overflowing =
        pci_mmio_va_limits{~astia::u64{0} - 0x7ffull, limit, limit};
    ASTIA_CHECK(!pci_mmio_range_fits_direct_map(0, 0x1000, overflowing));
}
