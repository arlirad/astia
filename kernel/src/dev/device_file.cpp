#include "device_file.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    long device_file::read(void* buf, u32 len, u64 offset) {
        return _device == nullptr
                   ? -EINVAL
                   : _device->read(astia::span<u8>(static_cast<u8*>(buf), len), offset);
    }

    long device_file::write(const void* buf, u32 len, u64 offset) {
        return _device == nullptr
                   ? -EINVAL
                   : _device->write(
                         astia::span<const u8>(static_cast<const u8*>(buf), len), offset);
    }

    long device_file::submit(io_request& request) {
        return _device == nullptr ? -EINVAL : _device->submit(request);
    }
    u32 device_file::poll(u32 events, u32 flags) {
        return _device == nullptr ? static_cast<u32>(POLLNVAL)
                                  : _device->poll(events, flags);
    }

    long device_file::ioctl(u64 request, void* argp, usize size) {
        return _device == nullptr
                   ? -EINVAL
                   : _device->ioctl(request,
                                     astia::span<u8>(static_cast<u8*>(argp), size));
    }

    long device_file::acquire_controlling_terminal(i32 group) {
        return _device == nullptr ? -EINVAL : _device->acquire_controlling_terminal(group);
    }

    long device_file::size() const {
        return -ESPIPE;
    }

} // namespace astia::dev
