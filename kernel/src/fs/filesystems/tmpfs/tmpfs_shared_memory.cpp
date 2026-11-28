#include <astia/mm/share.hpp>

#include "tmpfs_internal.hpp"

namespace astia::fs::filesystems {
    using tmpfs_detail::access_allowed;
    using tmpfs_detail::copy_path;
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

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (!entry_table()[i].used || !string_equal(entry_table()[i].path, path))
                continue;

            if (entry_table()[i].directory)
                return -EISDIR;

            if ((flags & oflag_creat) != 0 && (flags & oflag_excl) != 0)
                return -EEXIST;

            const u32 access = flags & ASTIA_AIO_O_ACCMODE;

            const permission_bits want =
                access == ASTIA_AIO_O_RDWR ? static_cast<permission_bits>(
                                                 static_cast<u8>(permission_bits::read) |
                                                 static_cast<u8>(permission_bits::write))
                : access == ASTIA_AIO_O_WRONLY ? permission_bits::write
                                               : permission_bits::read;

            if (!access_allowed(
                    entry_table()[i].target->mode(), entry_table()[i].target->uid(),
                    entry_table()[i].target->gid(), euid, egid, want, privileged))
                return -EACCES;

            result = mm::share<open_file>(entry_table()[i].target, flags);
            if (!result)
                return -ENOMEM;

            result->set_path(path);

            if ((flags & oflag_trunc) != 0 && access != 0)
                return entry_table()[i].target->truncate_ready(0);

            return 0;
        }

        if ((flags & oflag_creat) == 0)
            return -ENOENT;

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used)
                continue;

            auto file = mm::shptr<tmpfs_file>(new tmpfs_file{tmpfs_max_pages, mode});
            if (!file)
                return -ENOMEM;

            file->chown(uid, gid);

            if (!copy_path(entry_table()[i].path, path))
                return -ENAMETOOLONG;

            entry_table()[i].used      = true;
            entry_table()[i].directory = false;
            entry_table()[i].mode      = mode;
            entry_table()[i].target    = file;
            result                     = mm::share<open_file>(file, flags);
            if (!result) {
                entry_table()[i] = entry{};

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

            for (u32 i = 0; i < node_capacity(); ++i) {
                if (!entry_table()[i].used || !string_equal(entry_table()[i].path, path))
                    continue;

                if (entry_table()[i].directory)
                    return -EISDIR;

                file             = entry_table()[i].target;
                entry_table()[i] = entry{};

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
