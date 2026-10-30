#include "device_file.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    namespace {
        long device_file_read(fs::file* object, void* buffer, u32 length, u64 offset) {
            return static_cast<device_file*>(object)->read(buffer, length, offset);
        }
        long device_file_write(fs::file* object, const void* buffer, u32 length, u64 offset) {
            return static_cast<device_file*>(object)->write(buffer, length, offset);
        }
        long device_file_submit(fs::file* object, io_request& request) {
            return static_cast<device_file*>(object)->submit(request);
        }
        u32 device_file_poll(const fs::file* object, u32 events, u32 flags) {
            return const_cast<device_file*>(static_cast<const device_file*>(object))
                ->poll(events, flags);
        }
        long device_file_ioctl(fs::file* object, u64 request, void* argp) {
            return static_cast<device_file*>(object)->ioctl(request, argp);
        }
        long device_file_size(const fs::file*) {
            return -ESPIPE;
        }

        const fs::file_read_capability device_file_read_capability{device_file_read};
        const fs::file_write_capability device_file_write_capability{device_file_write};
        const fs::file_submit_capability device_file_submit_capability{device_file_submit};
        const fs::file_poll_capability device_file_poll_capability{device_file_poll};
        const fs::file_ioctl_capability device_file_ioctl_capability{device_file_ioctl};
        const fs::file_size_capability device_file_size_capability{device_file_size};
    }

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

    const void* device_file::capability(fs::file_capability kind) const {
        switch (kind) {
            case fs::file_capability::read:   return &device_file_read_capability;
            case fs::file_capability::write:  return &device_file_write_capability;
            case fs::file_capability::submit: return &device_file_submit_capability;
            case fs::file_capability::poll:   return &device_file_poll_capability;
            case fs::file_capability::ioctl:  return &device_file_ioctl_capability;
            case fs::file_capability::size:   return &device_file_size_capability;
            default:                          return fs::file::capability(kind);
        }
    }

} // namespace astia::dev
