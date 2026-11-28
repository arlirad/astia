#pragma once

#include <astia/common.hpp>

namespace astia::acpi::detail {
    /// Read a QEMU fw_cfg file by name.  The x86 fw_cfg interface is exposed
    /// through the selector/data ports; a missing device or file is reported
    /// as false, not as a fatal error.
    bool fw_cfg_size(const char* name, u32& size);
    bool fw_cfg_read(const char* name, u8* destination, u32 capacity);
} // namespace astia::acpi::detail
