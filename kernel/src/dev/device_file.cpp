#include "device_file.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    auto device_file::read(void* buf, u32 len, u64 offset) -> long {
        return _device == nullptr ? -EINVAL : _device->read(buf, len, offset);
    }

    auto device_file::write(const void* buf, u32 len, u64 offset) -> long {
        return _device == nullptr ? -EINVAL : _device->write(buf, len, offset);
    }

    auto device_file::ioctl(u64 request, void* argp) -> long {
        return _device == nullptr ? -EINVAL : _device->ioctl(request, argp);
    }

    auto device_file::size() const -> long {
        return -ESPIPE;
    }

} // namespace astia::dev
