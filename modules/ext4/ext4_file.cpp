#include <astia/api/errno.h>

#include "ext4.hpp"

namespace astia::ext4 {
    core::task<i64> file::readv(span<const fs::buffer> buffers, u64 offset,
                                     core::cltoken token) {
        return submit_readv(buffers, offset, token);
    }

    core::task<i64> file::writev(span<const fs::buffer> buffers, u64 offset,
                                      core::cltoken token, bool nonblock) {
        return submit_writev(buffers, offset, token, nonblock);
    }

    long file::read_ready(void* buffer, u32 length, u64 offset) {
        if (_node.type() != ext4::node_type::regular)
            return -EISDIR;

        if (offset >= _node.size)
            return 0;

        const u64 available = _node.size - offset;
        const u32 count     = available < length ? static_cast<u32>(available) : length;
        if (count == 0)
            return 0;

        return _owner->read_file(_node, buffer, count, offset) ? count : -EIO;
    }

    long file::size() const {
        return _node.size > static_cast<u64>(~i64{0} >> 1)
                   ? -EOVERFLOW
                   : static_cast<long>(_node.size);
    }

    fs::file_mode file::mode() const {
        return mount::vfs_mode(_node);
    }
} // namespace astia::ext4
