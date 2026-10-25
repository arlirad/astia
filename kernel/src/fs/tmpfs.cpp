#include "tmpfs.hpp"

#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::fs {

    namespace {
        // The async open ABI forwards the platform's O_* word.
        inline constexpr u32 oflag_write = 1;
        inline constexpr u32 oflag_rdwr  = 2;
        inline constexpr u32 oflag_creat = 0x04;
        inline constexpr u32 oflag_excl  = 0x08;
        inline constexpr u32 oflag_trunc = 0x10;

        bool equal(const char* lhs, const char* rhs) {
            if (lhs == nullptr || rhs == nullptr) {
                return false;
            }
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        bool copy_path(char* dst, const char* src) {
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

        bool parent_of(const char* parent, const char* path) {
            u32 n = 0;
            while (parent[n] != '\0') ++n;
            if (n == 1) return path[0] == '/';
            for (u32 i = 0; i < n; ++i) if (parent[i] != path[i]) return false;
            return path[n] == '/';
        }
    } // namespace

    tmpfs_file::tmpfs_file(usize pages, u32 mode)
        : storage_(mm::phys_region::create(pages, true)), mode_(mode) {}

    tmpfs_file::~tmpfs_file() {
        if (storage_ != nullptr) {
            storage_->release();
        }
    }

    long tmpfs_file::read(void* buffer, u32 length, u64 offset) {
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

    long tmpfs_file::write(const void* buffer, u32 length, u64 offset) {
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

    long tmpfs_file::touch(u64 sec, u64 nsec) {
        mtime_sec_ = sec;
        mtime_nsec_ = nsec;
        return 0;
    }

    long tmpfs_file::size() const {
        return static_cast<long>(size_);
    }

    mm::shptr<open_file> tmpfs_mount::open(const char* path, u32 flags) {
        if (path == nullptr || path[0] != '/') {
            return {};
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used || !equal(entries_[i].path, path)) {
                continue;
            }
            if (entries_[i].directory) return {};
            if ((flags & (oflag_write | oflag_rdwr)) != 0 && !(entries_[i].mode & 0200)) return {};
            if ((flags & oflag_write) == 0 && !(entries_[i].mode & 0400)) return {};
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
            entries_[i].directory = false;
            entries_[i].mode = 0100000 | (0666u & 07777u);
            entries_[i].object = object;
            return mm::share<open_file>(object, flags);
        }
        return {};
    }

    long tmpfs_mount::mkdir(const char* path, u32 mode) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        // The mount point is the tmpfs root.  From the VFS this is the
        // existing directory /tmp, so mkdir(/tmp) must have normal mkdir
        // semantics rather than looking like an invalid tmpfs pathname.
        if (equal(path, "/")) return -EEXIST;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) return -EEXIST;
        char parent[path_max]{};
        copy_path(parent, path);
        u32 end = 0;
        while (parent[end] != '\0') ++end;
        while (end > 1 && parent[end - 1] != '/') --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type(parent) != directory_dir) return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used) {
                entries_[i].used = true;
                entries_[i].directory = true;
                entries_[i].mode = 0040000 | (mode & 07777u);
                if (!copy_path(entries_[i].path, path)) return -ENAMETOOLONG;
                return 0;
            }
        }
        return -ENOSPC;
    }

    long tmpfs_mount::unlink(const char* path, bool directory) {
        // The mount root already exists.  Report EEXIST so `mkdir -p` can
        // walk through a mounted directory such as /tmp.
        if (path == nullptr || path[0] != '/') return -EINVAL;
        if (equal(path, "/")) return -EEXIST;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used || !equal(entries_[i].path, path)) continue;
            if (entries_[i].directory != directory) return directory ? -ENOTDIR : -EISDIR;
            if (directory) {
                for (u32 j = 0; j < tmpfs_max_entries; ++j)
                    if (entries_[j].used && parent_of(path, entries_[j].path)) return -ENOTEMPTY;
            }
            entries_[i] = entry{};
            return 0;
        }
        return -ENOENT;
    }

    long tmpfs_mount::rename(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        if (equal(old_path, "/")) return -EBUSY;
        if (equal(old_path, new_path)) return 0;

        char old_parent[path_max]{};
        char new_parent[path_max]{};
        if (!copy_path(old_parent, old_path) || !copy_path(new_parent, new_path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (old_parent[end] != '\0') ++end;
        while (end > 1 && old_parent[end - 1] != '/') --end;
        old_parent[end > 1 ? end - 1 : 1] = '\0';
        end = 0;
        while (new_parent[end] != '\0') ++end;
        while (end > 1 && new_parent[end - 1] != '/') --end;
        new_parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type(old_parent) != directory_dir || node_type(new_parent) != directory_dir)
            return -ENOENT;

        u32 source = tmpfs_max_entries;
        u32 target = tmpfs_max_entries;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, old_path)) source = i;
            if (entries_[i].used && equal(entries_[i].path, new_path)) target = i;
        }
        if (source == tmpfs_max_entries) return -ENOENT;
        if (target != tmpfs_max_entries) {
            if (entries_[source].directory != entries_[target].directory)
                return entries_[source].directory ? -ENOTDIR : -EISDIR;
            if (entries_[target].directory) {
                for (u32 i = 0; i < tmpfs_max_entries; ++i)
                    if (entries_[i].used && parent_of(new_path, entries_[i].path))
                        return -ENOTEMPTY;
            }
            entries_[target] = entries_[source];
            if (!copy_path(entries_[target].path, new_path)) return -ENAMETOOLONG;
            entries_[source] = entry{};
            return 0;
        }
        if (entries_[source].directory && parent_of(old_path, new_path)) return -EINVAL;
        if (!copy_path(entries_[source].path, new_path)) return -ENAMETOOLONG;
        return 0;
    }

    u32 tmpfs_mount::node_type(const char* path) const {
        if (path == nullptr || path[0] != '/') return directory_unknown;
        if (equal(path, "/")) return directory_dir;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return entries_[i].directory ? directory_dir : directory_reg;
        return directory_unknown;
    }

    u32 tmpfs_mount::node_mode(const char* path) const {
        if (path == nullptr || path[0] != '/') return 0;
        if (equal(path, "/")) return 0040755;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) return entries_[i].mode;
        return 0;
    }

    long tmpfs_mount::chmod(const char* path, u32 mode) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        if (equal(path, "/")) return 0; /* mount root is not an entry */
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path)) {
                entries_[i].mode = (entries_[i].mode & 0170000u) | (mode & 07777u);
                if (entries_[i].object) entries_[i].object->set_mode(entries_[i].mode);
                return 0;
            }
        }
        return -ENOENT;
    }

    fs::mount* tmpfs::mount(const char* source) const {
        (void) source;
        if (instance_ == nullptr) {
            instance_ = new tmpfs_mount{};
        }
        return instance_;
    }

} // namespace astia::fs
