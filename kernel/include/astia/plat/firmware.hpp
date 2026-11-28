#pragma once

#include <astia/common.hpp>

namespace astia::plat {
    /// Platform-provided raw ACPI table container.
    ///
    /// A platform that exposes firmware tables without an RSDP (QEMU fw_cfg,
    /// for example) implements these; everything else stays false.  The blob
    /// is the concatenation of ACPI tables, not a linked RSDP tree.
    bool acpi_tables_size(u32& size);
    bool acpi_tables_read(u8* destination, u32 capacity);
} // namespace astia::plat
