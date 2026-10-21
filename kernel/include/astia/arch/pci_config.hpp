#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    struct pci_bdf {
        u8 bus;
        u8 device;
        u8 function;
    };

    /// Read/write one PCI configuration-space dword.  An absent function
    /// reads as 0xffffffff.  The implementation is architecture/platform
    /// specific; the PCI bus enumerator is not.
    u32  pci_config_read32(pci_bdf address, u8 offset);
    void pci_config_write32(pci_bdf address, u8 offset, u32 value);

} // namespace astia::arch
