#include "tmpfs.hpp"

#include "share.hpp"
#include <astia/api/errno.h>

namespace astia::fs {

    namespace {
        // The async open ABI forwards the platform's O_* word.
        inline constexpr u32 oflag_write = 1;
        inline constexpr u32 oflag_rdwr  = 2;
        inline constexpr u32 oflag_creat = 0x04;
        inline constexpr u32 oflag_excl  = 0x08;
        inline constexpr u32 oflag_trunc = 0x10;

        auto equal(const char* lhs, const char* rhs) -> bool {
            if (lhs == nullptr || rhs == nullptr) {
                return false;
            }
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        auto copy_path(char* dst, const char* src) -> bool {
            if (src == nullptr || src[0] != '/') {
                return false;
            }
            u32 i = 0;
            for (; src[i] != '\0' && i + 1 < path_max; ++i) {
                dst[i] = src[i];
            }
            if (src[i] != '\0') {
                return false;
            }
            dst[i] = '\0';
            return true;
        }
    } // namespace

    tmpfs_file::tmpfs_file(usize pages)
        : storage_(mm::phys_region::create(pages, true)) {}

    tmpfs_file::~tmpfs_file() {
        if (storage_ != nullptr) {
            storage_->release();
        }
    }

    auto tmpfs_file::read(void* buffer, u32 length, u64 offset) -> long {
        if (buffer == nullptr && length != 0) {
            return -EFAULT;
        }
        if (offset >= size_) {
            return 0;
        }
        const u64   count  = (size_ - offset) < length ? size_ - offset : length;
        const auto* source = static_cast<const u8*>(storage_->page_direct(0));
        auto*       target = static_cast<u8*>(buffer);
        for (u64 i = 0; i < count; ++i) {
            target[i] = source[offset + i];
        }
        return static_cast<long>(count);
    }

    auto tmpfs_file::write(const void* buffer, u32 length, u64 offset) -> long {
        if (buffer == nullptr && length != 0) {
            return -EFAULT;
        }
        const u64 capacity = tmpfs_max_pages * mm::phys_region::frame_size;
        if (offset >= capacity || static_cast<u64>(length) > capacity - offset) {
            return -ENOSPC;
        }
        const auto* source = static_cast<const u8*>(buffer);
        auto*       target = static_cast<u8*>(storage_->page_direct(0));
        for (u64 i = 0; i < length; ++i) {
            target[offset + i] = source[i];
        }
        if (offset + length > size_) {
            size_ = offset + length;
        }
        return static_cast<long>(length);
    }

    auto tmpfs_file::size() const -> long {
        return static_cast<long>(size_);
    }

    auto tmpfs_mount::open(const char* path, u32 flags) -> mm::shptr<open_file> {
        if (path == nullptr || path[0] != '/') {
            return {};
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used || !equal(entries_[i].path, path)) {
                continue;
            }
            if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0) {
                return {};
            }
            if ((flags & oflag_trunc) != 0 && (flags & (oflag_write | oflag_rdwr)) != 0) {
                entries_[i].object->truncate();
            }
            return mm::share<open_file>(entries_[i].object, flags);
        }
        if ((flags & oflag_creat) == 0) {
            return {};
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used) {
                continue;
            }
            auto object = mm::shptr<tmpfs_file>(new tmpfs_file{});
            if (!object || !copy_path(entries_[i].path, path)) {
                return {};
            }
            entries_[i].used   = true;
            entries_[i].object = object;
            return mm::share<open_file>(object, flags);
        }
        return {};
    }

    auto tmpfs::mount(const char* source) const -> fs::mount* {
        (void) source;
        if (instance_ == nullptr) {
            instance_ = new tmpfs_mount{};
        }
        return instance_;
    }

} // namespace astia::fs
