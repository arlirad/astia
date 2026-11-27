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
        return mm::usable_top(map, top);
    }

    /// Extent the arch direct map actually maps: the usable top rounded up.
    /// False when the usable top cannot be rounded up without wrapping.
    inline bool mapped_limit(const mm::memory_map& map, u64& limit) {
        u64 top = 0;

        return mm::usable_top(map, top) &&
               checked_align_up(top, direct_map_alignment, limit);
    }

    /// Firmware-table window for the ACPI reader: the highest firmware-
    /// described range, rounded up and capped at `cap` (the arch's direct-map
    /// span). Reads inside the window but above `mapped_limit` go through the
    /// on-demand mapping fallback. False when a range wraps or the rounded
    /// top does not fit in a u64.
    inline bool direct_map_limit(const mm::memory_map& map, u64 cap, u64& limit) {
        u64 top = 0;

        if (!mm::direct_map_top(map, top) ||
            !checked_align_up(top, direct_map_alignment, top))
            return false;

        limit = top > cap ? cap : top;

        return true;
    }
} // namespace astia::acpi::detail
