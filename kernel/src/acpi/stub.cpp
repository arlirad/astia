#include <astia/acpi.hpp>

namespace astia::acpi {
    u64 rsdp() {
        return 0;
    }

    u64 ioapic_base() {
        return 0;
    }

    u8 gsi_for_irq(u8 irq) {
        return irq;
    }

    bool resolve_pci_route(u8, u8, u8, u8, pci_route& out) {
        out = {};

        return false;
    }

    bool shutdown_supported() {
        return false;
    }

    bool shutdown() {
        return false;
    }

    bool reboot_supported() {
        return false;
    }

    bool reboot() {
        return false;
    }
} // namespace astia::acpi
