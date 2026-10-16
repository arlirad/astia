#pragma once

#include "device.hpp"
#include "fs.hpp"

// Filesystem-facing adapter for a hardware device.  The device remains
// a device; this object is the file users and the async ring operate on.

namespace astia::dev {

    class device_file : public fs::file {
        public:
        constexpr explicit device_file(device* device) : _device(device) {}

        auto read(void* buf, u32 len, u64 offset) -> long override;
        auto write(const void* buf, u32 len, u64 offset) -> long override;
        auto ioctl(u64 request, void* argp) -> long override;
        auto size() const -> long override;

        private:
        device* _device;
    };

} // namespace astia::dev
