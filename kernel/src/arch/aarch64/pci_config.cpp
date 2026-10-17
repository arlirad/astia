#include <astia/arch/pci_config.hpp>

namespace astia::arch {

    u32 pci_config_read32(pci_bdf, u8) {
        return ~u32{0};
    }

    void pci_config_write32(pci_bdf, u8, u32) {}

} // namespace astia::arch
