#include "block_device.hpp"

namespace astia::dev::blk {

    bool valid(const block_device& device) {
        return device.ops != nullptr &&
               device.ops->abi_version == block_device_abi_version &&
               device.ops->read_at != nullptr && device.size_bytes != 0 &&
               device.block_size != 0;
    }

    bool read_at(const block_device& device, u64 offset, void* buffer, usize length) {
        if (!valid(device) || (buffer == nullptr && length != 0) ||
            offset > device.size_bytes || length > device.size_bytes - offset)
            return false;
        return device.ops->read_at(device.context, offset, buffer, length);
    }

    bool read_at_adapter(void* context, u64 offset, void* buffer, usize length) {
        if (context == nullptr)
            return false;
        return read_at(*static_cast<const block_device*>(context), offset, buffer, length);
    }

} // namespace astia::dev::blk
