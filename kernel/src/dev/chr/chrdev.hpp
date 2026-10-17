#pragma once

#include "../device.hpp"

// Character device: moves bytes, not sectors — no geometry, no
// alignment, no offsets that matter.  Everything device already
// offers applies; chrdev is the char arm of the blk/char/net split
// and the base of the terminal classes.

namespace astia::dev {

    class chrdev : public device {
        public:
        using device::device;
    };

} // namespace astia::dev
