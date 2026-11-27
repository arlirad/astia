#pragma once

#include <astia/common.hpp>

#include "tables.hpp"

namespace astia::acpi::detail {
    enum class madt_error : u8 {
        none,
        bad_rsdp,
        bad_root_table,
        bad_table_length,
        bad_table_checksum,
        bad_entry,
        duplicate_apic_id,
        too_many_cpus,
        no_cpus,
        no_madt,
    };

    bool discover_madt(const memory_reader& reader, u64 rsdp_address, u32* apic_ids,
                       u32 capacity, u32& count, madt_error& error);
} // namespace astia::acpi::detail
