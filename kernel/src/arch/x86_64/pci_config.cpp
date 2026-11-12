#include "io.hpp"
#include <astia/arch/pci_config.hpp>

namespace astia::arch {

    namespace {
        constexpr u32 config_enable       = 0x80000000u;
        constexpr u16 config_address_port = 0x0cf8;
        constexpr u16 config_data_port    = 0x0cfc;
        constexpr u32 config_offset_mask  = 0xfcu;

        inline u32 config_address(const pci_bdf address, u16 offset) {
            return config_enable | (static_cast<u32>(address.bus) << 16) |
                   (static_cast<u32>(address.device) << 11) |
                   (static_cast<u32>(address.function) << 8) |
                   (offset & config_offset_mask);
        }
    }

    u32 pci_config_read32(const pci_bdf address, u16 offset) {
        if (!address.valid() || offset > 0xfcu || (offset & 3u) != 0)
            return ~u32{0};
        x86_64::outl(config_address_port, config_address(address, offset));
        return x86_64::inl(config_data_port);
    }

    void pci_config_write32(const pci_bdf address, u16 offset, u32 value) {
        if (!address.valid() || offset > 0xfcu || (offset & 3u) != 0)
            return;
        x86_64::outl(config_address_port, config_address(address, offset));
        x86_64::outl(config_data_port, value);
    }

} // namespace astia::arch
