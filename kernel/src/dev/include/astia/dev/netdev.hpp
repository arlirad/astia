#pragma once

#include <astia/dev/device.hpp>

// Network device: packet semantics sit above the same ring-facing
// read/write seam, while framing, queues, and link state belong to the
// concrete driver.  This is the net arm of the device split; no fake
// packet API is added until the networking ABI exists.

namespace astia::dev {

    class netdev : public device {
        public:
        constexpr netdev(const char* name, unsigned flags)
            : device(name, flags | dv_net) {}
    };

} // namespace astia::dev
