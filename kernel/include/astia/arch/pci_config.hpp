#pragma once

#include <astia/common.hpp>

namespace astia::arch {
    struct pci_bdf {
        u8 bus;
        u8 device;
        u8 function;

        constexpr bool valid() const {
            return device < 32 && function < 8;
        }

        constexpr bool operator==(pci_bdf other) const {
            return bus == other.bus && device == other.device &&
                   function == other.function;
        }

        constexpr bool operator!=(pci_bdf other) const {
            return !(*this == other);
        }
    };

    /// Read/write one PCI configuration-space dword.  An absent function
    /// reads as 0xffffffff.  The implementation is architecture/platform
    /// specific; the PCI bus enumerator is not.
    u32  pci_config_read32(pci_bdf address, u16 offset);
    void pci_config_write32(pci_bdf address, u16 offset, u32 value);
    void pci_config_write8(pci_bdf address, u16 offset, u8 value);
} // namespace astia::arch
