#pragma once

#include "driver.hpp"

namespace astia::dev {

    /// Enumerate PCI configuration space and offer every discovered function
    /// to the registered driver manager.  Returns the number of functions
    /// discovered, not the number successfully bound.
    u32 pci_enumerate();

} // namespace astia::dev
