#pragma once

#include "device.hpp"
#include <astia/fs.hpp>

// Filesystem-facing adapter for a hardware device.  The device remains
// a device; this object is the file users and the async ring operate on.

namespace astia::dev {

    class device_file : public fs::file {
        public:
        constexpr explicit device_file(device* device) : _device(device) {}

        long read(void* buf, u32 len, u64 offset) override;
        long write(const void* buf, u32 len, u64 offset) override;
        long submit(io_request& request) override;
        long ioctl(u64 request, void* argp) override;
        long size() const override;

        private:
        device* _device;
    };

} // namespace astia::dev
