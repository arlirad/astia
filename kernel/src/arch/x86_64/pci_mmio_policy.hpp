#pragma once

#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/user_layout.hpp>

namespace astia::arch::x86_64 {
    /// The boot identity window [0, pci_identity_window_size) is mapped
    /// write-back and aliases the same physical pages as the direct map.
    inline constexpr u64 pci_identity_window_size = 1ull << 30;

    /// Physical floor below which a BAR would alias an existing write-back
    /// mapping: max(identity window, page-aligned usable-RAM top).
    constexpr u64 pci_mmio_safe_floor(u64 direct_map_top) {
        return direct_map_top > pci_identity_window_size ? direct_map_top
                                                         : pci_identity_window_size;
    }

    struct pci_mmio_va_limits {
        u64 kernel_base      = 0;
        u64 direct_map_limit = 0;
        u64 reserved_floor   = 0;
    };

    constexpr bool pci_mmio_va_is_canonical(u64 address) {
        return astia::arch::user_layout_policy::is_canonical(address);
    }

    inline constexpr u64 pci_mmio_canonical_low_limit =
        astia::arch::user_layout_policy::address_limit;
    inline constexpr u64 pci_mmio_canonical_high_base =
        ~(pci_mmio_canonical_low_limit - 1);

    /// The whole inclusive range [first, last_end] must lie in one canonical
    /// half. Checking endpoints alone would accept an interval spanning the
    /// x86 noncanonical hole.
    constexpr bool pci_mmio_va_span_is_canonical(u64 first, u64 last_end) {
        if (!pci_mmio_va_is_canonical(first) || !pci_mmio_va_is_canonical(last_end))
            return false;

        const bool low_half = first < pci_mmio_canonical_low_limit &&
                              last_end < pci_mmio_canonical_low_limit;
        const bool high_half = first >= pci_mmio_canonical_high_base &&
                               last_end >= pci_mmio_canonical_high_base;

        return low_half || high_half;
    }

    constexpr pci_mmio_va_limits pci_mmio_va_limits_for(u64 kernel_base,
                                                        u64 direct_map_limit,
                                                        u64 mmio_base, u64 recursive_base,
                                                        u64 image_base) {
        u64       reserved_floor = ~u64{0};
        const u64 mmio_offset    = mmio_base > kernel_base ? mmio_base - kernel_base : 0;
        const u64 recursive_offset =
            recursive_base > kernel_base ? recursive_base - kernel_base : 0;
        const u64 image_offset = image_base > kernel_base ? image_base - kernel_base : 0;

        if (mmio_offset != 0 && mmio_offset < reserved_floor)
            reserved_floor = mmio_offset;

        if (recursive_offset != 0 && recursive_offset < reserved_floor)
            reserved_floor = recursive_offset;

        if (image_offset != 0 && image_offset < reserved_floor)
            reserved_floor = image_offset;

        return {kernel_base, direct_map_limit,
                reserved_floor == ~u64{0} ? 0 : reserved_floor};
    }

    /// The page-rounded physical extent may use `kernel_base + physical` only
    /// while the whole span stays canonical, inside the direct-map segment,
    /// and below every reserved higher-half window. The contiguous bounds make
    /// this an O(1) endpoint/boundary test rather than a per-page scan.
    constexpr bool pci_mmio_range_fits_direct_map(u64 first, u64 last,
                                                  const pci_mmio_va_limits& limits) {
        if (limits.kernel_base == 0 || limits.direct_map_limit == 0 ||
            limits.reserved_floor == 0)
            return false;

        if ((first & static_cast<u64>(mm::page_mask)) != 0 ||
            (last & static_cast<u64>(mm::page_mask)) != 0 || first > last)
            return false;

        const u64 last_end = last | static_cast<u64>(mm::page_mask);

        if (last_end >= limits.direct_map_limit || last_end >= limits.reserved_floor)
            return false;

        const u64 va_first    = limits.kernel_base + first;
        const u64 va_last     = limits.kernel_base + last;
        const u64 va_last_end = va_last | static_cast<u64>(mm::page_mask);

        return va_first >= limits.kernel_base && va_last >= va_first &&
               pci_mmio_va_span_is_canonical(va_first, va_last_end);
    }

    enum class pci_mmio_range {
        accepted,
        invalid,
        write_back_alias,
    };

    /// A BAR may only be mapped where no write-back alias already exists.
    /// The BAR start is page-rounded before the floor comparison: a BAR that
    /// begins just below the floor still shares its first page with it.
    constexpr pci_mmio_range pci_mmio_range_check(u64 physical, u64 size,
                                                  u64 write_back_floor) {
        if (size == 0 || physical > ~u64{0} - (size - 1))
            return pci_mmio_range::invalid;

        if ((physical & ~static_cast<u64>(mm::page_mask)) < write_back_floor)
            return pci_mmio_range::write_back_alias;

        return pci_mmio_range::accepted;
    }

    struct pci_mmio_page_span {
        u64 first = 0;
        u64 last  = 0;
    };

    constexpr pci_mmio_page_span pci_mmio_page_span_of(u64 physical, u64 size) {
        return {physical & ~static_cast<u64>(mm::page_mask),
                (physical + size - 1) & ~static_cast<u64>(mm::page_mask)};
    }

    constexpr bool pci_mmio_spans_overlap(const pci_mmio_page_span& first,
                                          const pci_mmio_page_span& second) {
        return first.first <= second.last && second.first <= first.last;
    }

    /// Cache policy for the PCI BAR mapper. Every BAR it exposes is
    /// register/device MMIO, so map it strict uncached: with PAT disabled,
    /// PCD=1 is UC; with PAT enabled and the reset/default IA32_PAT,
    /// PCD=1/PWT=1 with PATi=0 selects PAT entry 3 (UC), whereas PCD alone
    /// selects entry 2 (UC-). PCI prefetchable describes device read side
    /// effects, not permission for write-back caching; a WC policy for
    /// prefetchable BARs would need its own explicit mapping API.
    constexpr page_flags pci_bar_page_flags(bool /*prefetchable*/) {
        return astia::arch::mmio_device_page_flags();
    }
} // namespace astia::arch::x86_64
