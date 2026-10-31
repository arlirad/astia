#include "tmpfs.hpp"

#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::fs {

    namespace {
        u64 next_inode = 2;
        // The async open ABI forwards the platform's O_* word.
        inline constexpr u32 oflag_write = ASTIA_AIO_O_WRONLY;
        inline constexpr u32 oflag_rdwr  = ASTIA_AIO_O_RDWR;
        inline constexpr u32 oflag_creat = ASTIA_AIO_O_CREAT;
        inline constexpr u32 oflag_excl  = ASTIA_AIO_O_EXCL;
        inline constexpr u32 oflag_trunc = ASTIA_AIO_O_TRUNC;

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
            while (parent[n] != '\0')
                ++n;
            if (n == 1)
                return path[0] == '/';
            for (u32 i = 0; i < n; ++i)
                if (parent[i] != path[i])
                    return false;
            return path[n] == '/';
        }

        mm::phys_region* tmpfs_mapping(const file* object) {
            return const_cast<tmpfs_file*>(static_cast<const tmpfs_file*>(object))
                ->memory_region();
        }

        const file_mapping_capability tmpfs_mapping_capability{tmpfs_mapping};
    } // namespace

    const void* tmpfs_file::capability(file_capability kind) const {
        switch (kind) {
        case file_capability::mapping:
            return &tmpfs_mapping_capability;
        default:
            return file::capability(kind);
        }
    }

    tmpfs_file::tmpfs_file(usize pages, file_mode mode)
        : storage_(mm::phys_region::create(pages, true)), mode_(mode),
          inode_(__atomic_fetch_add(&next_inode, 1u, __ATOMIC_RELAXED)) {}

    tmpfs_file::~tmpfs_file() {
        if (storage_ != nullptr) {
            storage_->release();
        }
    }

    long tmpfs_file::read(void* buffer, u32 length, u64 offset) {
        core::spinlock_guard guard(lock_);
        if (buffer == nullptr && length != 0) {
            return -EFAULT;
        }
        if (storage_ == nullptr)
            return -EIO;
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
        core::spinlock_guard guard(lock_);
        if (buffer == nullptr && length != 0) {
            return -EFAULT;
        }
        if (storage_ == nullptr)
            return -EIO;
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
        core::spinlock_guard guard(lock_);
        mtime_sec_  = sec;
        mtime_nsec_ = nsec;
        return 0;
    }

    long tmpfs_file::size() const {
        core::spinlock_guard guard(lock_);
        return static_cast<long>(size_);
    }

    mm::shptr<open_file> tmpfs_mount::open(const char* path, u32 flags) {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || path[0] != '/') {
            return {};
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used || !equal(entries_[i].path, path)) {
                continue;
            }
            if (entries_[i].directory)
                return {};
            /* Permission selection is performed by the process/VFS ABI
               boundary, where real/effective and supplementary credentials
               are available.  Do not accidentally re-apply owner bits here. */
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
        char parent[path_max]{};
        if (!copy_path(parent, path))
            return {};
        u32 end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return {};
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used) {
                continue;
            }
            auto object = mm::shptr<tmpfs_file>(new tmpfs_file{});
            if (!object || dispatch_mapping(object.get()) == nullptr ||
                !copy_path(entries_[i].path, path)) {
                return {};
            }
            entries_[i].used      = true;
            entries_[i].directory = false;
            entries_[i].mode      = file_mode(file_type::regular, default_file_permissions);
            entries_[i].object    = object;
            return mm::share<open_file>(object, flags);
        }
        return {};
    }

    long tmpfs_mount::mkdir(const char* path, file_mode mode) {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        // The mount point is the tmpfs root.  From the VFS this is the
        // existing directory /tmp, so mkdir(/tmp) must have normal mkdir
        // semantics rather than looking like an invalid tmpfs pathname.
        if (equal(path, "/"))
            return -EEXIST;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return -EEXIST;
        char parent[path_max]{};
        copy_path(parent, path);
        u32 end = 0;
        while (parent[end] != '\0')
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used) {
                if (!copy_path(entries_[i].path, path))
                    return -ENAMETOOLONG;
                entries_[i].used      = true;
                entries_[i].directory = true;
                entries_[i].mode =
                    file_mode(file_type::directory, mode.permissions().chmod_mask());
                return 0;
            }
        }
        return -ENOSPC;
    }

    long tmpfs_mount::symlink(const char* target, const char* path) {
        core::spinlock_guard guard(namespace_lock_);
        if (target == nullptr || path == nullptr || path[0] != '/' || target[0] == '\0')
            return -EINVAL;
        if (node_type_unlocked(path) != directory_unknown)
            return -EEXIST;
        char parent[path_max]{};
        if (!copy_path(parent, path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (parent[end])
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (!entries_[i].used) {
                u32 n = 0;
                while (target[n]) {
                    if (n + 1 >= path_max)
                        return -ENAMETOOLONG;
                    ++n;
                }
                auto object = mm::shptr<tmpfs_file>(new tmpfs_file{
                    1, file_mode(file_type::symlink, default_symlink_permissions)});
                if (!object || !copy_path(entries_[i].path, path))
                    return -ENOMEM;
                if (dispatch_write(object.get(), target, n, 0) != (long) n)
                    return -ENOSPC;
                entries_[i].used      = true;
                entries_[i].directory = false;
                entries_[i].mode = file_mode(file_type::symlink, default_symlink_permissions);
                entries_[i].object = object;
                return 0;
            }
        return -ENOSPC;
    }
    long tmpfs_mount::readlink(const char* path, char* buffer, u32 capacity) const {
        core::spinlock_guard guard(namespace_lock_);
        if (!buffer || capacity == 0)
            return -EINVAL;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) {
                if (!entries_[i].mode.is_symlink())
                    return -EINVAL;
                long n = dispatch_read(entries_[i].object.get(), buffer, capacity, 0);
                if (n < 0)
                    return n;
                return n;
            }
        return -ENOENT;
    }

    long tmpfs_mount::read_directory(const char* path, u64 index, char* name,
                                     u32 capacity, file_type* type) {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;
        if (node_type_unlocked(path) != directory_dir)
            return -ENOTDIR;
        u32 path_len = 0;
        while (path[path_len] != '\0')
            ++path_len;
        u64 ordinal = 0;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used)
                continue;
            const char* full  = entries_[i].path;
            u32         start = path_len == 1 ? 1 : path_len + 1;
            if (path_len != 1) {
                bool prefix = true;
                for (u32 j = 0; j < path_len; ++j)
                    if (full[j] != path[j])
                        prefix = false;
                if (!prefix || full[path_len] != '/')
                    continue;
            }
            u32 end = start;
            while (full[end] != '\0' && full[end] != '/')
                ++end;
            if (end == start || end >= capacity)
                continue;
            bool duplicate = false;
            for (u32 p = 0; p < i && !duplicate; ++p) {
                if (!entries_[p].used)
                    continue;
                const char* prior       = entries_[p].path;
                u32         prior_start = path_len == 1 ? 1 : path_len + 1;
                if (path_len != 1) {
                    bool prior_prefix = true;
                    for (u32 j = 0; j < path_len; ++j)
                        if (prior[j] != path[j])
                            prior_prefix = false;
                    if (!prior_prefix || prior[path_len] != '/')
                        continue;
                }
                u32 prior_end = prior_start;
                while (prior[prior_end] != '\0' && prior[prior_end] != '/')
                    ++prior_end;
                if (prior_end - prior_start == end - start) {
                    duplicate = true;
                    for (u32 j = 0; j < end - start; ++j)
                        if (prior[prior_start + j] != full[start + j])
                            duplicate = false;
                }
            }
            if (duplicate || ordinal++ != index)
                continue;
            for (u32 j = 0; j < end - start; ++j)
                name[j] = full[start + j];
            name[end - start] = '\0';
            *type = end < path_max && full[end] != '\0'
                        ? directory_dir
                        : (entries_[i].directory
                               ? directory_dir
                               : (entries_[i].mode.is_symlink() ? directory_lnk
                                                                : directory_reg));
            return 1;
        }
        return 0;
    }

    long tmpfs_mount::unlink(const char* path, bool directory) {
        core::spinlock_guard guard(namespace_lock_);
        // The mount root already exists.  Report EEXIST so `mkdir -p` can
        // walk through a mounted directory such as /tmp.
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        if (equal(path, "/"))
            return -EEXIST;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used || !equal(entries_[i].path, path))
                continue;
            if (entries_[i].directory != directory)
                return directory ? -ENOTDIR : -EISDIR;
            if (directory) {
                for (u32 j = 0; j < tmpfs_max_entries; ++j)
                    if (entries_[j].used && parent_of(path, entries_[j].path))
                        return -ENOTEMPTY;
            }
            if (entries_[i].object)
                entries_[i].object->drop_link();
            entries_[i] = entry{};
            return 0;
        }
        return -ENOENT;
    }

    long tmpfs_mount::rename(const char* old_path, const char* new_path) {
        core::spinlock_guard guard(namespace_lock_);
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        if (equal(old_path, "/"))
            return -EBUSY;
        if (equal(old_path, new_path))
            return 0;

        char old_parent[path_max]{};
        char new_parent[path_max]{};
        if (!copy_path(old_parent, old_path) || !copy_path(new_parent, new_path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (old_parent[end] != '\0')
            ++end;
        while (end > 1 && old_parent[end - 1] != '/')
            --end;
        old_parent[end > 1 ? end - 1 : 1] = '\0';
        end                               = 0;
        while (new_parent[end] != '\0')
            ++end;
        while (end > 1 && new_parent[end - 1] != '/')
            --end;
        new_parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(old_parent) != directory_dir ||
            node_type_unlocked(new_parent) != directory_dir)
            return -ENOENT;

        u32 source = tmpfs_max_entries;
        u32 target = tmpfs_max_entries;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, old_path))
                source = i;
            if (entries_[i].used && equal(entries_[i].path, new_path))
                target = i;
        }
        if (source == tmpfs_max_entries)
            return -ENOENT;
        if (entries_[source].directory && parent_of(old_path, new_path))
            return -EINVAL;
        if (target != tmpfs_max_entries) {
            if (entries_[source].directory != entries_[target].directory)
                return entries_[source].directory ? -ENOTDIR : -EISDIR;
            if (entries_[target].directory) {
                for (u32 i = 0; i < tmpfs_max_entries; ++i)
                    if (entries_[i].used && parent_of(new_path, entries_[i].path))
                        return -ENOTEMPTY;
            }
            if (entries_[target].object)
                entries_[target].object->drop_link();
            entries_[target] = entry{};
        }
        const u32 old_len = [&] {
            u32 n = 0;
            while (old_path[n])
                ++n;
            return n;
        }();
        const u32 new_len = [&] {
            u32 n = 0;
            while (new_path[n])
                ++n;
            return n;
        }();
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used)
                continue;
            bool match = equal(entries_[i].path, old_path);
            if (!match && entries_[source].directory)
                match = parent_of(old_path, entries_[i].path);
            if (!match)
                continue;
            u32       suffix     = equal(entries_[i].path, old_path) ? old_len : old_len;
            const u32 suffix_len = [&] {
                u32 n = 0;
                while (entries_[i].path[suffix + n])
                    ++n;
                return n;
            }();
            if (new_len + suffix_len >= path_max)
                return -ENAMETOOLONG;
        }
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!entries_[i].used)
                continue;
            bool match = equal(entries_[i].path, old_path);
            if (!match && entries_[source].directory)
                match = parent_of(old_path, entries_[i].path);
            if (!match)
                continue;
            char renamed[path_max]{};
            for (u32 j = 0; j < new_len; ++j)
                renamed[j] = new_path[j];
            u32 suffix = old_len;
            u32 at     = new_len;
            while (entries_[i].path[suffix])
                renamed[at++] = entries_[i].path[suffix++];
            renamed[at] = '\0';
            copy_path(entries_[i].path, renamed);
        }
        return 0;
    }

    long tmpfs_mount::link(const char* old_path, const char* new_path) {
        core::spinlock_guard guard(namespace_lock_);
        if (old_path == nullptr || new_path == nullptr)
            return -EINVAL;
        if (node_type_unlocked(old_path) != directory_reg)
            return node_type_unlocked(old_path) == directory_dir ? -EPERM : -ENOENT;
        if (node_type_unlocked(new_path) != directory_unknown)
            return -EEXIST;
        char parent[path_max]{};
        if (!copy_path(parent, new_path))
            return -ENAMETOOLONG;
        u32 end = 0;
        while (parent[end])
            ++end;
        while (end > 1 && parent[end - 1] != '/')
            --end;
        parent[end > 1 ? end - 1 : 1] = '\0';
        if (node_type_unlocked(parent) != directory_dir)
            return -ENOENT;
        u32 source = tmpfs_max_entries;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, old_path))
                source = i;
        if (source == tmpfs_max_entries)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (!entries_[i].used) {
                if (!copy_path(entries_[i].path, new_path))
                    return -ENAMETOOLONG;
                entries_[i] = entry{true,
                                    false,
                                    entries_[source].mode,
                                    entries_[source].uid,
                                    entries_[source].gid,
                                    {},
                                    entries_[source].object};
                copy_path(entries_[i].path, new_path);
                entries_[source].object->add_link();
                return 0;
            }
        return -ENOSPC;
    }

    file_type tmpfs_mount::node_type(const char* path) const {
        core::spinlock_guard guard(namespace_lock_);
        return node_type_unlocked(path);
    }

    file_type tmpfs_mount::node_type_unlocked(const char* path) const {
        if (path == nullptr || path[0] != '/')
            return directory_unknown;
        if (equal(path, "/"))
            return directory_dir;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return entries_[i].directory
                           ? directory_dir
                           : (entries_[i].mode.is_symlink() ? directory_lnk
                                                            : directory_reg);
        return directory_unknown;
    }

    file_mode tmpfs_mount::node_mode(const char* path) const {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || path[0] != '/')
            return {};
        if (equal(path, "/"))
            return file_mode(file_type::directory, readonly_directory_permissions);
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return entries_[i].mode;
        return {};
    }
    uid_t tmpfs_mount::node_uid(const char* path) const {
        core::spinlock_guard guard(namespace_lock_);
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return entries_[i].uid;
        return {};
    }
    gid_t tmpfs_mount::node_gid(const char* path) const {
        core::spinlock_guard guard(namespace_lock_);
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return entries_[i].gid;
        return {};
    }

    long tmpfs_mount::chmod(const char* path, file_permissions permissions) {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        if (equal(path, "/"))
            return 0; /* mount root is not an entry */
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path)) {
                entries_[i].mode = entries_[i].mode.chmod(permissions);
                if (entries_[i].object)
                    entries_[i].object->set_mode(entries_[i].mode);
                return 0;
            }
        }
        return -ENOENT;
    }
    long tmpfs_mount::chown(const char* path, uid_t uid, gid_t gid) {
        core::spinlock_guard guard(namespace_lock_);
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        for (u32 i = 0; i < tmpfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) {
                entries_[i].uid = uid;
                entries_[i].gid = gid;
                if (entries_[i].object)
                    (void) entries_[i].object->chown(uid, gid);
                return 0;
            }
        return -ENOENT;
    }

    fs::mount* tmpfs::mount(const char* source) const {
        (void) source;
        return new tmpfs_mount{};
    }

} // namespace astia::fs
