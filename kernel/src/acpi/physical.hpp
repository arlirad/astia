#pragma once

#include <astia/arch/layout.hpp>
#include <astia/hw/mmio.hpp>
#include <astia/mm/memory_map.hpp>

#include "limits.hpp"
#include "reader.hpp"

namespace astia::acpi::detail {
    inline bool read_through_mapping(void*, u64 address, u8* destination, usz length) {
        if (destination == nullptr)
            return false;

        auto mapping = astia::hw::mmio_mapping::map(address, length);
        if (!mapping)
            return false;

        __builtin_memcpy(destination, const_cast<void*>(mapping.value.base()), length);

        return true;
    }

    /// Build the kernel reader for `map`. False when the map cannot describe
    /// the direct-map extent or the firmware-table window.
    inline bool kernel_physical_reader(const mm::memory_map& map, u64 identity_limit,
                                       physical_reader& reader) {
        u64 mapped = 0;
        u64 window = 0;

        if (!mapped_limit(map, mapped) ||
            !direct_map_limit(map, astia::arch::direct_map_limit, window))
            return false;

        reader = {
            identity_limit,       mapped,  window, astia::arch::kernel_base,
            read_through_mapping, nullptr,
        };

        return true;
    }
} // namespace astia::acpi::detail
