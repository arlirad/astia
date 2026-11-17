#include <astia/dev/blk/block_device.hpp>

#include <astia/api/errno.h>

namespace astia::dev::blk {

    long block_device::validate(const void* buffer, u32 length, u64 offset) const {
        if (block_size_ == 0 || (offset % block_size_) != 0 ||
            (length % block_size_) != 0 || (buffer == nullptr && length != 0))
            return -EINVAL;
        if (offset > size_bytes_ || length > size_bytes_ - offset)
            return -EINVAL;
        return 0;
    }

    long block_device::read(void* buffer, u32 length, u64 offset) {
        const long valid = validate(buffer, length, offset);
        return valid != 0 ? valid : read_blocks(buffer, length, offset);
    }

    long block_device::write(const void* buffer, u32 length, u64 offset) {
        const long valid = validate(buffer, length, offset);
        return valid != 0 ? valid : write_blocks(buffer, length, offset);
    }

    long block_device::submit(io_request& request) {
        const long valid = validate(request.buffer(), request.length(), request.offset());
        return valid != 0 ? valid : device::submit(request);
    }

    long block_device::write_blocks(const void*, u32, u64) {
        return -EROFS;
    }

} // namespace astia::dev::blk
