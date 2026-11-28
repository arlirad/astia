#include "tmpfs_internal.hpp"

namespace astia::fs::filesystems {
    file_type tmpfs_mount::node_type(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        return node_type_unlocked(path);
    }

    file_type tmpfs_mount::node_type_unlocked(const char* path) const {
        if (path == nullptr || path[0] != '/')
            return directory_unknown;

        if (string_equal(path, "/"))
            return directory_dir;

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].directory
                           ? directory_dir
                           : (entry_table()[i].mode.is_symlink() ? directory_lnk
                                                                 : directory_reg);
        }

        return directory_unknown;
    }

    file_mode tmpfs_mount::node_mode(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return {};

        if (string_equal(path, "/"))
            return file_mode(file_type::directory, readonly_directory_permissions);

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].target ? entry_table()[i].target->mode()
                                               : entry_table()[i].mode;
        }

        return {};
    }

    u64 tmpfs_mount::node_inode(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return 0;

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].target ? entry_table()[i].target->inode() : 0;
        }

        return 0;
    }

    long tmpfs_mount::node_path(u64 inode, char* buffer, u32 capacity) const {
        if (buffer == nullptr || capacity == 0)
            return -EINVAL;

        core::spinlock_guard guard(_namespace_lock);

        if (inode == 0) {
            if (capacity < 2)
                return -ENAMETOOLONG;

            buffer[0] = '/';
            buffer[1] = '\0';

            return 0;
        }

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (!entry_table()[i].used || !entry_table()[i].directory ||
                !entry_table()[i].target)
                continue;

            if (entry_table()[i].target->inode() != inode)
                continue;

            u32 length = 0;

            while (entry_table()[i].path[length] != '\0') {
                ++length;
            }

            if (length + 1 > capacity)
                return -ENAMETOOLONG;

            for (u32 j = 0; j <= length; ++j) {
                buffer[j] = entry_table()[i].path[j];
            }

            return 0;
        }

        return -ENOENT;
    }

    uid_t tmpfs_mount::node_uid(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].target ? entry_table()[i].target->uid() : uid_t{};
        }

        return {};
    }

    gid_t tmpfs_mount::node_gid(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].target ? entry_table()[i].target->gid() : gid_t{};
        }

        return {};
    }

    file_timestamps tmpfs_mount::node_timestamps(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return {};

        if (string_equal(path, "/")) {
            if (_root_times.atime_sec == 0 && _root_times.mtime_sec == 0 &&
                _root_times.ctime_sec == 0)
                _root_times = current_timestamps();

            return _root_times;
        }

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path))
                return entry_table()[i].target ? entry_table()[i].target->timestamps()
                                               : file_timestamps{};
        }

        return {};
    }

    long tmpfs_mount::chmod(const char* path, file_permissions permissions) {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        if (string_equal(path, "/"))
            return 0; /* mount root is not an entry */

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (entry_table()[i].used && string_equal(entry_table()[i].path, path)) {
                entry_table()[i].mode = entry_table()[i].mode.chmod(permissions);

                if (entry_table()[i].target)
                    entry_table()[i].target->set_mode(entry_table()[i].mode);

                return 0;
            }
        }

        return -ENOENT;
    }

    long tmpfs_mount::chown(const char* path, uid_t uid, gid_t gid) {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        for (u32 i = 0; i < node_capacity(); ++i) {
            if (!entry_table()[i].used || !string_equal(entry_table()[i].path, path))
                continue;

            if (!entry_table()[i].target)
                return -ENOENT;

            return entry_table()[i].target->chown(uid, gid);
        }

        return -ENOENT;
    }
} // namespace astia::fs::filesystems
