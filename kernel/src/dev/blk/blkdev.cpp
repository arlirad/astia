#include <astia/api/errno.h>
#include <astia/dev/blk/blkdev.hpp>

namespace astia::dev::blk {
    long blkdev::validate(const void* buffer, u32 length, u64 offset) const {
        if (block_size_ == 0 || (offset % block_size_) != 0 ||
            (length % block_size_) != 0 || (buffer == nullptr && length != 0))
            return -EINVAL;

        if (offset > size_bytes_ || length > size_bytes_ - offset)
            return -EINVAL;

        return 0;
    }

    long blkdev::read(void* buffer, u32 length, u64 offset) {
        const long valid = validate(buffer, length, offset);

        return valid != 0 ? valid : read_blocks(buffer, length, offset);
    }

    long blkdev::write(const void* buffer, u32 length, u64 offset) {
        const long valid = validate(buffer, length, offset);

        return valid != 0 ? valid : write_blocks(buffer, length, offset);
    }

    long blkdev::submit(io::request& request) {
        const long valid = validate(request.buffer(), request.length(), request.offset());

        return valid != 0 ? valid : device::submit(request);
    }

    long blkdev::write_blocks(const void*, u32, u64) {
        return -EROFS;
    }
} // namespace astia::dev::blk
