#pragma once

#include <astia/common.hpp>

namespace astia::mm {
    /// Address range types as reported by the firmware/hypervisor map. The
    /// values match the x86/HVM (PVH) ABI, which in turn matches the ACPI
    /// "System Address Map Interfaces" range types.
    enum class memory_type : u32 {
        usable           = 1,
        reserved         = 2,
        acpi_reclaimable = 3,
        acpi_nvs         = 4,
        unusable         = 5,
        disabled         = 6,
        persistent       = 7,
    };

    struct memory_region {
        u64         base = 0;
        u64         size = 0;
        memory_type type = memory_type::reserved;
    };

    /// End address of a region. False when `base + size` wraps.
    inline bool region_end(const memory_region& region, u64& end) {
        if (region.base > ~u64{0} - region.size)
            return false;

        end = region.base + region.size;

        return true;
    }

    /// True when the region is part of the direct-map contract: every
    /// firmware-described range except explicitly unusable or disabled
    /// address space. ACPI tables may live in any of these ranges.
    inline bool direct_map_type(memory_type type) {
        return type != memory_type::unusable && type != memory_type::disabled;
    }

    /// A fixed-capacity physical memory map. The PVH option ROM stores at
    /// most 128 e820 entries, which we mirror here.
    class memory_map {
        public:
        static constexpr usz max_regions = 128;

        void add(u64 base, u64 size, memory_type type) {
            if (_count >= max_regions)
                return;

            _regions[_count++] = {base, size, type};
        }

        usz count() const {
            return _count;
        }

        const memory_region* begin() const {
            return _regions;
        }

        const memory_region* end() const {
            return _regions + _count;
        }

        const memory_region& operator[](usz index) const {
            return _regions[index];
        }

        private:
        memory_region _regions[max_regions]{};
        usz           _count = 0;
    };

    /// Highest end address of a usable region. False when any usable region
    /// wraps past the end of the address space.
    inline bool usable_top(const memory_map& map, u64& top) {
        top = 0;

        for (const auto& region : map) {
            if (region.type != memory_type::usable)
                continue;

            u64 end = 0;

            if (!region_end(region, end))
                return false;

            if (end > top)
                top = end;
        }

        return true;
    }

    /// Highest end address of a directly mappable region. False when any such
    /// region wraps past the end of the address space.
    inline bool direct_map_top(const memory_map& map, u64& top) {
        top = 0;

        for (const auto& region : map) {
            if (!direct_map_type(region.type))
                continue;

            u64 end = 0;

            if (!region_end(region, end))
                return false;

            if (end > top)
                top = end;
        }

        return true;
    }
} // namespace astia::mm
