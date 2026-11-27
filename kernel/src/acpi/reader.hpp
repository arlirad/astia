#pragma once

#include <astia/arch/layout.hpp>
#include <astia/common.hpp>

#include "tables.hpp"

namespace astia::acpi::detail {
    struct physical_reader {
        u64 identity_limit = 0;
        u64 direct_limit   = 0;
    };

    inline bool read_physical(void* context, u64 address, u8* destination, usz length) {
        const auto* reader = static_cast<const physical_reader*>(context);

        if (reader == nullptr || destination == nullptr || address > ~u64{0} - length)
            return false;

        const u64 end = address + length;

        if (end <= reader->identity_limit) {
            __builtin_memcpy(destination, reinterpret_cast<const void*>(address), length);

            return true;
        }

        if (end <= reader->direct_limit) {
            __builtin_memcpy(
                destination,
                reinterpret_cast<const void*>(astia::arch::kernel_base + address),
                length);

            return true;
        }

        return false;
    }

    inline memory_reader physical_memory_reader(physical_reader& reader) {
        return {
            &reader,
            read_physical,
        };
    }
} // namespace astia::acpi::detail
