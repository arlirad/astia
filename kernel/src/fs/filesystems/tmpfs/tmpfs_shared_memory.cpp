#include <astia/mm/share.hpp>

#include "tmpfs_internal.hpp"

namespace astia::fs::filesystems {

    using tmpfs_detail::access_allowed;
    using tmpfs_detail::copy_path;
    using tmpfs_detail::equal;
    using tmpfs_detail::oflag_creat;
    using tmpfs_detail::oflag_excl;
    using tmpfs_detail::oflag_trunc;

    namespace {
        tmpfs_mount shared_memory_mount;
    } // namespace

    long tmpfs_mount::open_shared(const char* path, u32 flags, file_mode mode, uid_t uid,
                                  gid_t gid, uid_t euid, gid_t egid, bool privileged,
                                  mm::shptr<open_file>& result) {
        if (path == nullptr || path[0] != '/' || path[1] == '\0')
            return -EINVAL;
        core::spinlock_guard guard(_namespace_lock);
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (!_entries[i].used || !equal(_entries[i].path, path))
                continue;
            if (_entries[i].directory)
                return -EISDIR;
            if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0)
                return -EEXIST;
            const u32             access = flags & ASTIA_AIO_O_ACCMODE;
            const permission_bits want =
                access == ASTIA_AIO_O_RDWR ? static_cast<permission_bits>(
                                                 static_cast<u8>(permission_bits::read) |
                                                 static_cast<u8>(permission_bits::write))
                : access == ASTIA_AIO_O_WRONLY ? permission_bits::write
                                               : permission_bits::read;
            if (!access_allowed(_entries[i].mode, _entries[i].uid, _entries[i].gid, euid,
                                egid, want, privileged))
                return -EACCES;
            result = mm::share<open_file>(_entries[i].target, flags);
            if (!result)
                return -ENOMEM;
            result->set_path(path);
            if ((flags & oflag_trunc) != 0 && access != 0)
                return _entries[i].target->truncate_ready(0);
            return 0;
        }
        if ((flags & oflag_creat) == 0)
            return -ENOENT;
        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used)
                continue;
            auto file = mm::shptr<tmpfs_file>(new tmpfs_file{tmpfs_max_pages, mode});
            if (!file)
                return -ENOMEM;
            file->chown(uid, gid);
            if (!copy_path(_entries[i].path, path))
                return -ENAMETOOLONG;
            _entries[i].used      = true;
            _entries[i].directory = false;
            _entries[i].mode      = mode;
            _entries[i].uid       = uid;
            _entries[i].gid       = gid;
            _entries[i].target    = file;
            result                = mm::share<open_file>(file, flags);
            if (!result) {
                _entries[i] = entry{};
                return -ENOMEM;
            }
            result->set_path(path);
            return 0;
        }
        return -ENOSPC;
    }

    long tmpfs_mount::unlink_shared(const char* path) {
        if (path == nullptr || path[0] != '/' || path[1] == '\0')
            return -EINVAL;
        mm::shptr<tmpfs_file> file;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < tmpfs_max_entries; ++i) {
                if (!_entries[i].used || !equal(_entries[i].path, path))
                    continue;
                if (_entries[i].directory)
                    return -EISDIR;
                file        = _entries[i].target;
                _entries[i] = entry{};
                break;
            }
        }
        if (!file)
            return -ENOENT;
        file->drop_link();
        return 0;
    }

} // namespace astia::fs::filesystems

namespace astia::fs {

    long open_shared_memory(const char* path, u32 flags, file_mode mode, uid_t uid,
                            gid_t gid, uid_t euid, gid_t egid, bool privileged,
                            mm::shptr<open_file>& result) {
        return filesystems::shared_memory_mount.open_shared(
            path, flags, mode, uid, gid, euid, egid, privileged, result);
    }

    long unlink_shared_memory(const char* path) {
        return filesystems::shared_memory_mount.unlink_shared(path);
    }

} // namespace astia::fs
