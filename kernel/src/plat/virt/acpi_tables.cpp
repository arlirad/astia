#include <astia/plat/firmware.hpp>

#include "fwcfg.hpp"

namespace astia::plat {
    bool acpi_tables_size(u32& size) {
        return fw_cfg::file_size("etc/acpi/tables", size);
    }

    bool acpi_tables_read(u8* destination, u32 capacity) {
        return fw_cfg::read_file("etc/acpi/tables", destination, capacity);
    }
} // namespace astia::plat
