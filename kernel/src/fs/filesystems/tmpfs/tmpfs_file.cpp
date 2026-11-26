#include <astia/api/errno.h>
#include <astia/atomic.hpp>

#include "tmpfs.hpp"

namespace astia::fs::filesystems {
    namespace {
        u64 next_inode = 2;
    } // namespace

    tmpfs_file::tmpfs_file(usize pages, file_mode mode)
        : _storage(mode.is_directory() ? nullptr : mm::phys_region::create(pages, true)),
          _mode(mode), _inode(astia::atomic_ref<u64>(next_inode)
                                  .fetch_add(1u, astia::memory_order::relaxed)) {}

    tmpfs_file::~tmpfs_file() {
        if (_storage != nullptr)
            _storage->release();
    }

    core::task<i64> tmpfs_file::readv(span<const buffer> buffers, u64 offset,
                                      core::cltoken token) {
        return submit_readv(buffers, offset, token);
    }

    core::task<i64> tmpfs_file::writev(span<const buffer> buffers, u64 offset,
                                       core::cltoken token, bool nonblock) {
        return submit_writev(buffers, offset, token, nonblock);
    }

    long tmpfs_file::read_ready(void* buffer, u32 length, u64 offset) {
        core::spinlock_guard guard(_lock);

        if (buffer == nullptr && length != 0)
            return -EFAULT;

        if (_storage == nullptr)
            return -EIO;

        if (offset >= _size)
            return 0;

        const u64 count = (_size - offset) < length ? _size - offset : length;

        const auto* source = static_cast<const u8*>(_storage->page_direct(0));
        auto*       target = static_cast<u8*>(buffer);

        for (u64 i = 0; i < count; ++i) {
            target[i] = source[offset + i];
        }

        return static_cast<long>(count);
    }

    long tmpfs_file::write_ready(const void* buffer, u32 length, u64 offset) {
        core::spinlock_guard guard(_lock);

        if (buffer == nullptr && length != 0)
            return -EFAULT;

        if (_storage == nullptr)
            return -EIO;

        const u64 capacity = tmpfs_max_pages * mm::phys_region::frame_size;

        if (offset >= capacity || static_cast<u64>(length) > capacity - offset)
            return -ENOSPC;

        const auto* source = static_cast<const u8*>(buffer);
        auto*       target = static_cast<u8*>(_storage->page_direct(0));

        for (u64 i = 0; i < length; ++i) {
            target[offset + i] = source[i];
        }

        if (offset + length > _size)
            _size = offset + length;

        return static_cast<long>(length);
    }

    core::task<i64> tmpfs_file::sync(core::cltoken) {
        core::spinlock_guard guard(_lock);

        if (_storage == nullptr || !_mode.is_regular())
            co_return -EINVAL;

        astia::atomic_thread_fence(astia::memory_order::seq_cst);

        co_return 0;
    }

    core::task<i64> tmpfs_file::sync_mapping(mm::phys_region* mapping, u64 offset,
                                             u64 length, u32, core::cltoken) {
        const u64 page_bytes = mm::phys_region::frame_size;

        if (length == 0 || (offset % page_bytes) != 0 || (length % page_bytes) != 0 ||
            offset > ~u64{0} - length || mapping != _storage || _storage == nullptr ||
            offset >= tmpfs_max_pages * page_bytes)
            co_return -EINVAL;

        if (length > tmpfs_max_pages * page_bytes - offset)
            co_return -EINVAL;

        core::spinlock_guard guard(_lock);

        const u64 range_end = offset + length;

        for (u64 page_offset = 0; page_offset < length; page_offset += page_bytes) {
            const u64 current       = offset + page_offset;
            const u64 page_end      = current + page_bytes;
            const u64 visible_start = current < _size ? current : _size;
            const u64 visible_end   = page_end < _size ? page_end : _size;

            const auto* source = static_cast<const u8*>(
                mapping->page_direct(static_cast<usize>(current / page_bytes)));
            auto* target = static_cast<u8*>(
                _storage->page_direct(static_cast<usize>(current / page_bytes)));

            if (source == nullptr || target == nullptr)
                co_return -EIO;

            if (visible_start < visible_end) {
                const u64 first = visible_start - current;
                const u64 count = visible_end - visible_start;

                for (u64 i = 0; i < count; ++i) {
                    target[first + i] = source[first + i];
                }
            }

            const u64 zero_start = visible_end > current ? visible_end : current;
            const u64 zero_end   = page_end < range_end ? page_end : range_end;

            for (u64 i = zero_start; i < zero_end; ++i) {
                target[i - current] = 0;
            }
        }

        astia::atomic_thread_fence(astia::memory_order::seq_cst);

        co_return 0;
    }

    core::task<i64> tmpfs_file::truncate(u64 length, core::cltoken) {
        co_return truncate_ready(length);
    }

    long tmpfs_file::truncate_ready(u64 length) {
        core::spinlock_guard guard(_lock);

        const u64 capacity = tmpfs_max_pages * mm::phys_region::frame_size;

        if (length > capacity)
            return -ENOSPC;

        auto* bytes = static_cast<u8*>(_storage->page_direct(0));

        for (u64 i = _size; i < length; ++i) {
            bytes[i] = 0;
        }

        _size = length;

        return 0;
    }

    long tmpfs_file::touch(u64 sec, u64 nsec) {
        core::spinlock_guard guard(_lock);

        _mtime_sec  = sec;
        _mtime_nsec = nsec;

        return 0;
    }

    long tmpfs_file::size() const {
        core::spinlock_guard guard(_lock);

        return static_cast<long>(_size);
    }
} // namespace astia::fs::filesystems
