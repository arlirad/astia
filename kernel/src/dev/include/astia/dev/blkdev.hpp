#pragma once

#include <astia/dev/device.hpp>

// Block device: the ring supplies a byte buffer and an SQE offset;
// concrete block drivers interpret that offset as their media position
// and may impose sector alignment/size rules.  Queueing and completion
// remain the ring engine's job, so this is deliberately smaller than
// aretev's iopkt/blkqueue layer.

namespace astia::dev {

    class blkdev : public device {
        public:
        constexpr blkdev(const char* name, unsigned flags)
            : device(name, flags | dv_block) {}

        /// Logical sector size exposed by the device.  Concrete
        /// drivers may change it during initialization.
        u32 sector_size = 512;
    };

} // namespace astia::dev
