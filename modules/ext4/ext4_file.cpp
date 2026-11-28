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
        if (_owner == nullptr)
            return -EIO;

        if (!_node.is_regular())
            return _node.is_directory() ? -EISDIR : -EINVAL;

        return _owner->read_file(_node, buffer, length, offset);
    }

    long file::write_ready(const void* buffer, u32 length, u64 offset) {
        if (_owner == nullptr)
            return -EIO;

        if (_owner->read_only())
            return -EROFS;

        if (!_node.is_regular())
            return _node.is_directory() ? -EISDIR : -EINVAL;

        return _owner->write_file(_node, buffer, length, offset);
    }

    long file::size() const {
        return _node.size() > static_cast<u64>(~i64{0} >> 1)
                   ? -EOVERFLOW
                   : static_cast<long>(_node.size());
    }

    fs::file_mode file::mode() const {
        return mount::vfs_mode(_node);
    }

    core::task<i64> file::truncate(u64 length, core::cltoken token) {
        (void) token;

        if (_owner == nullptr)
            co_return -EIO;

        co_return _owner->truncate_file(_node, length);
    }

    core::task<i64> file::sync(core::cltoken token) {
        (void) token;

        if (_owner == nullptr)
            co_return -EIO;

        if (_owner->read_only())
            co_return -EROFS;

        co_return _owner->sync() ? i64{0} : -EIO;
    }

    long file::chmod(fs::file_permissions permissions) {
        if (_owner == nullptr)
            return -EIO;

        return _owner->chmod_node(_node, permissions);
    }

    long file::chown(uid_t uid, gid_t gid) {
        if (_owner == nullptr)
            return -EIO;

        return _owner->chown_node(_node, uid, gid);
    }

    long file::touch(const fs::file_timestamps& times) {
        if (_owner == nullptr)
            return -EIO;

        return _owner->touch_node(_node, times);
    }
} // namespace astia::ext4
