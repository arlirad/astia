#include "io.hpp"
#include <astia/arch/pci_config.hpp>

namespace astia::arch {

    namespace {
        inline u32 config_address(const pci_bdf address, u8 offset) {
            return 0x80000000u | (static_cast<u32>(address.bus) << 16) |
                   (static_cast<u32>(address.device) << 11) |
                   (static_cast<u32>(address.function) << 8) | (offset & 0xfcu);
        }
    }

    u32 pci_config_read32(const pci_bdf address, u8 offset) {
        x86_64::outl(0xcf8, config_address(address, offset));
        return x86_64::inl(0xcfcu);
    }

    void pci_config_write32(const pci_bdf address, u8 offset, u32 value) {
        x86_64::outl(0xcf8, config_address(address, offset));
        x86_64::outl(0xcfcu, value);
    }

} // namespace astia::arch
