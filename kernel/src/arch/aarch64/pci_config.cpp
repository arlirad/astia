#include <astia/arch/pci_config.hpp>

namespace astia::arch {
    u32 pci_config_read32(pci_bdf address, u16 offset) {
        if (!address.valid() || offset > 0xffcu || (offset & 3u) != 0)
            return ~u32{0};

        return ~u32{0};
    }

    void pci_config_write32(pci_bdf address, u16 offset, u32) {
        if (!address.valid() || offset > 0xffcu || (offset & 3u) != 0)
            return;
    }
} // namespace astia::arch
