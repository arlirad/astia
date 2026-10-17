#pragma once

#include <astia/common.hpp>

namespace astia::dev {

    struct pci_bar {
        u64  address      = 0;
        bool io           = false;
        bool prefetchable = false;
    };

    struct pci_device {
        u8      bus                   = 0;
        u8      device                = 0;
        u8      function              = 0;
        u16     vendor_id             = 0;
        u16     device_id             = 0;
        u8      revision              = 0;
        u8      class_code            = 0;
        u8      subclass              = 0;
        u8      programming_interface = 0;
        u8      header_type           = 0;
        pci_bar bars[6]{};
    };

    /// Enumerate PCI configuration space and offer every discovered function
    /// to the registered driver manager.  Returns the number of functions
    /// discovered, not the number successfully bound.
    u32 pci_enumerate();

} // namespace astia::dev
