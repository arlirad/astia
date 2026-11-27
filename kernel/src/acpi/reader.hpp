#pragma once

#include <astia/common.hpp>

#include "tables.hpp"

namespace astia::acpi::detail {
    struct physical_reader {
        u64 identity_limit              = 0;
        u64 mapped_limit                = 0;
        u64 direct_limit                = 0;
        u64 direct_base                 = 0;
        bool (*read_mapped)(void* context, u64 address, u8* destination,
                            usz length) = nullptr;
        void* mapped_context            = nullptr;
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

        if (end <= reader->mapped_limit && address <= ~u64{0} - reader->direct_base) {
            __builtin_memcpy(destination,
                             reinterpret_cast<const void*>(reader->direct_base + address),
                             length);

            return true;
        }

        if (end <= reader->direct_limit && reader->read_mapped != nullptr)
            return reader->read_mapped(reader->mapped_context, address, destination,
                                       length);

        return false;
    }

    inline memory_reader physical_memory_reader(physical_reader& reader) {
        return {
            &reader,
            read_physical,
        };
    }
} // namespace astia::acpi::detail
