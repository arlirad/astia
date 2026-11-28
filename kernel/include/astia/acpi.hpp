#pragma once

#include <astia/common.hpp>

namespace astia::acpi {
    /// Physical address of the loader-provided RSDP after its header has been
    /// validated, or 0 when ACPI is absent or the RSDP is invalid. A missing
    /// RSDP is the normal outcome on AArch64/FDT platforms.
    u64 rsdp();

    /// IOAPIC physical base discovered through ACPI MADT (0 when unknown).
    u64 ioapic_base();

    /// Translate a legacy ISA/PCI interrupt line to its GSI using MADT
    /// interrupt source overrides.
    u8 gsi_for_irq(u8 irq);

    /// True when the FADT, DSDT, \_S5 and PM1 control registers all passed
    /// validation and the ACPI platform shutdown handler is registered.
    bool shutdown_supported();

    /// Enter the kernel shutdown sequence. Earlier registered handlers run in
    /// tier order; ACPI S5 is the final platform handler.
    bool shutdown();

    /// True when the FADT advertises a validated reset register that the
    /// kernel can write without guessing at a legacy reset path.
    bool reboot_supported();

    /// Request a firmware reset through the validated FADT reset register.
    /// False means no reset register was available or the write failed; a
    /// successful reset does not return.
    bool reboot();
} // namespace astia::acpi
