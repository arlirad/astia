#include <astia/api/errno.h>
#include <astia/mapping.hpp>

#include "ext4.hpp"

namespace astia::ext4 {
    file::~file() {
        if (_owner != nullptr)
            _owner->release_inode_reference(_node.number);
    }

    mm::shptr<file> file::create(mount* owner, const inode_record& node) {
        if (owner == nullptr || !owner->acquire_inode(node.number))
            return {};

        auto handle = mm::share<file>(owner, node);

        if (!handle)
            (void) owner->release_inode(node.number);

        return handle;
    }

    core::task<i64> file::readv(span<const fs::buffer> buffers, u64 offset,
                                core::cltoken token) {
        if (_owner == nullptr)
            co_return error_t::ebadf;

        if (!_owner->refresh_inode(_node))
            co_return error_t::eio;

        if (!_node.is_regular())
            co_return error_from_errno(_node.is_directory() ? EISDIR : EINVAL);

        i64 total = 0;

        for (usz i = 0; i < buffers.size(); ++i) {
            const fs::buffer& entry = buffers[i];

            if (entry.length == 0)
                continue;

            if (token.canceled()) {
                if (total != 0)
                    co_return total;

                co_return error_t::ecanceled;
            }

            if (entry.length > static_cast<usz>(~u32{0})) {
                if (total != 0)
                    co_return total;

                co_return error_t::erange;
            }

            auto result = co_await _owner->read_file_async(
                _node, entry.data, static_cast<u32>(entry.length),
                offset + static_cast<u64>(total), token);

            if (!result) {
                if (total != 0)
                    co_return total;

                co_return result.error;
            }

            if (result.value < 0) {
                if (total != 0)
                    co_return total;

                co_return error_from_errno(-result.value);
            }

            total += result.value;

            if (token.canceled()) {
                if (total != 0)
                    co_return total;

                co_return error_t::ecanceled;
            }

            if (static_cast<usz>(result.value) < entry.length)
                break;
        }

        co_return total;
    }

    core::task<i64> file::writev(span<const fs::buffer> buffers, u64 offset,
                                 core::cltoken token, bool nonblock) {
        // An O_NONBLOCK regular-file write cannot be rejected at submission:
        // the coroutine path never spins, it suspends and is resumed by the
        // device completion, so `nonblock` means "do not busy-wait".
        (void) nonblock;

        if (_owner == nullptr)
            co_return error_t::ebadf;

        if (_owner->read_only())
            co_return error_t::erofs;

        if (!_owner->refresh_inode(_node))
            co_return error_t::eio;

        if (!_node.is_regular())
            co_return error_from_errno(_node.is_directory() ? EISDIR : EINVAL);

        i64 total = 0;

        for (usz i = 0; i < buffers.size(); ++i) {
            const fs::buffer& entry = buffers[i];

            if (entry.length == 0)
                continue;

            if (token.canceled()) {
                if (total != 0)
                    co_return total;

                co_return error_t::ecanceled;
            }

            if (entry.length > static_cast<usz>(~u32{0})) {
                if (total != 0)
                    co_return total;

                co_return error_t::erange;
            }

            auto result = co_await _owner->write_file_async(
                _node, entry.data, static_cast<u32>(entry.length),
                offset + static_cast<u64>(total), token);

            if (!result) {
                if (total != 0)
                    co_return total;

                co_return result.error;
            }

            if (result.value < 0) {
                if (total != 0)
                    co_return total;

                co_return error_from_errno(-result.value);
            }

            total += result.value;

            if (token.canceled()) {
                if (total != 0)
                    co_return total;

                co_return error_t::ecanceled;
            }

            if (static_cast<usz>(result.value) < entry.length)
                break;
        }

        co_return total;
    }

    long file::read_ready(void* buffer, u32 length, u64 offset) {
        if (_owner == nullptr)
            return -EIO;

        if (!_owner->refresh_inode(_node))
            return -EIO;

        if (!_node.is_regular())
            return _node.is_directory() ? -EISDIR : -EINVAL;

        return _owner->read_file(_node, buffer, length, offset);
    }

    long file::write_ready(const void* buffer, u32 length, u64 offset) {
        if (_owner == nullptr)
            return -EIO;

        if (!_owner->refresh_inode(_node))
            return -EIO;

        if (_owner->read_only())
            return -EROFS;

        if (!_node.is_regular())
            return _node.is_directory() ? -EISDIR : -EINVAL;

        return _owner->write_file(_node, buffer, length, offset);
    }

    long file::size() const {
        if (_owner == nullptr)
            return -EIO;

        const u64 on_disk = _owner->inode_size(_node.number);

        return on_disk > static_cast<u64>(~i64{0} >> 1)
                   ? -EOVERFLOW
                   : static_cast<long>(on_disk);
    }

    fs::file_mode file::mode() const {
        return mount::vfs_mode(_node);
    }

    core::task<i64> file::truncate(u64 length, core::cltoken token) {
        (void) token;

        if (_owner == nullptr)
            co_return -EIO;

        if (!_owner->refresh_inode(_node))
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

        if (!_owner->refresh_inode(_node))
            return -EIO;

        return _owner->chmod_node(_node, permissions);
    }

    long file::chown(uid_t uid, gid_t gid) {
        if (_owner == nullptr)
            return -EIO;

        if (!_owner->refresh_inode(_node))
            return -EIO;

        return _owner->chown_node(_node, uid, gid);
    }

    long file::touch(const fs::file_timestamps& times) {
        if (_owner == nullptr)
            return -EIO;

        if (!_owner->refresh_inode(_node))
            return -EIO;

        return _owner->touch_node(_node, times);
    }
} // namespace astia::ext4
