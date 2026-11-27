#pragma once

#include <astia/common.hpp>
#include <astia/mm/memory_map.hpp>

namespace astia::acpi::detail {
    /// Rounding granularity for the ACPI reader's direct-map window.
    constexpr u64 direct_map_alignment = 2ull << 20;

    /// True when `left + right` fits in a u64.
    inline bool checked_add(u64 left, u64 right, u64& result) {
        if (left > ~u64{0} - right)
            return false;

        result = left + right;

        return true;
    }

    /// True when `value` rounded up to `alignment` fits in a u64.
    /// `alignment` must be a non-zero power of two.
    inline bool checked_align_up(u64 value, u64 alignment, u64& result) {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return false;

        const u64 mask = alignment - 1;

        if (value > ~u64{0} - mask)
            return false;

        result = (value + mask) & ~mask;

        return true;
    }

    /// Highest end address of a usable region. False when any usable region
    /// wraps past the end of the address space.
    inline bool usable_top(const mm::memory_map& map, u64& top) {
        top = 0;

        for (const auto& region : map) {
            if (region.type != mm::memory_type::usable)
                continue;

            u64 end = 0;

            if (!checked_add(region.base, region.size, end))
                return false;

            if (end > top)
                top = end;
        }

        return true;
    }

    /// Direct-map limit for the ACPI reader. False when the usable top cannot
    /// be computed or rounded up without wrapping.
    inline bool direct_map_limit(const mm::memory_map& map, u64& limit) {
        u64 top = 0;

        return usable_top(map, top) && checked_align_up(top, direct_map_alignment, limit);
    }
} // namespace astia::acpi::detail
