#include "device_file.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    long device_file::read(void* buf, u32 len, u64 offset) {
        return _device == nullptr ? -EINVAL : _device->read(buf, len, offset);
    }

    long device_file::write(const void* buf, u32 len, u64 offset) {
        return _device == nullptr ? -EINVAL : _device->write(buf, len, offset);
    }

    long device_file::submit(io_request& request) {
        return _device == nullptr ? -EINVAL : _device->submit(request);
    }
    u32 device_file::poll(u32 events, u32 flags) {
        return _device == nullptr ? static_cast<u32>(POLLNVAL)
                                  : _device->poll(events, flags);
    }

    long device_file::ioctl(u64 request, void* argp) {
        return _device == nullptr ? -EINVAL : _device->ioctl(request, argp);
    }

    long device_file::size() const {
        return -ESPIPE;
    }

} // namespace astia::dev
