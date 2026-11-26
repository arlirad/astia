#pragma once

#include <astia/common.hpp>

namespace astia::acpi {
    /// Physical address of the loader-provided RSDP after its header has been
    /// validated, or 0 when ACPI is absent or the RSDP is invalid. A missing
    /// RSDP is the normal outcome on AArch64/FDT platforms.
    u64 rsdp();

    /// True when the FADT, DSDT, \_S5 and PM1 control registers all passed
    /// validation and the ACPI platform shutdown handler is registered.
    bool shutdown_supported();

    /// Enter the kernel shutdown sequence. Earlier registered handlers run in
    /// tier order; ACPI S5 is the final platform handler.
    bool shutdown();
} // namespace astia::acpi
