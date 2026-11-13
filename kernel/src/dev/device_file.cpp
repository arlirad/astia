#include "device_file.hpp"
#include "blk/block_device.hpp"

#include <astia/api/errno.h>

namespace astia::dev {

    long device_file::read(void* buf, u32 len, u64 offset) {
        device* value = device_instance();
        return value == nullptr
                   ? -EINVAL
                   : value->read(astia::span<u8>(static_cast<u8*>(buf), len), offset);
    }

    long device_file::write(const void* buf, u32 len, u64 offset) {
        device* value = device_instance();
        return value == nullptr
                   ? -EINVAL
                   : value->write(
                         astia::span<const u8>(static_cast<const u8*>(buf), len), offset);
    }

    long device_file::submit(io_request& request) {
        device* value = device_instance();
        return value == nullptr ? -EINVAL : value->submit(request);
    }
    u32 device_file::poll(u32 events, u32 flags) {
        device* value = device_instance();
        return value == nullptr ? static_cast<u32>(POLLNVAL) : value->poll(events, flags);
    }

    long device_file::ioctl(u64 request, void* argp, usize size) {
        device* value = device_instance();
        return value == nullptr
                   ? -EINVAL
                   : value->ioctl(request, astia::span<u8>(static_cast<u8*>(argp), size));
    }

    long device_file::acquire_controlling_terminal(i32 group) {
        device* value = device_instance();
        return value == nullptr ? -EINVAL : value->acquire_controlling_terminal(group);
    }

    long device_file::size() const {
        device* value = device_instance();
        if (value != nullptr && (value->flags() & dv_block) != 0)
            return static_cast<long>(static_cast<const blk::block_device*>(value)->size_bytes());
        return -ESPIPE;
    }

    u32 device_file::block_size() const {
        device* value = device_instance();
        if (value != nullptr && (value->flags() & dv_block) != 0)
            return static_cast<const blk::block_device*>(value)->block_size();
        return 0;
    }

} // namespace astia::dev
