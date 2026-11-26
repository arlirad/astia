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

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path))
                return _entries[i].directory
                           ? directory_dir
                           : (_entries[i].mode.is_symlink() ? directory_lnk
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

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path))
                return _entries[i].target ? _entries[i].target->mode() : _entries[i].mode;
        }

        return {};
    }

    uid_t tmpfs_mount::node_uid(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path))
                return _entries[i].uid;
        }

        return {};
    }

    gid_t tmpfs_mount::node_gid(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path))
                return _entries[i].gid;
        }

        return {};
    }

    long tmpfs_mount::chmod(const char* path, file_permissions permissions) {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        if (string_equal(path, "/"))
            return 0; /* mount root is not an entry */

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path)) {
                _entries[i].mode = _entries[i].mode.chmod(permissions);

                if (_entries[i].target)
                    _entries[i].target->set_mode(_entries[i].mode);

                return 0;
            }
        }

        return -ENOENT;
    }

    long tmpfs_mount::chown(const char* path, uid_t uid, gid_t gid) {
        core::spinlock_guard guard(_namespace_lock);

        if (path == nullptr || path[0] != '/')
            return -EINVAL;

        for (u32 i = 0; i < tmpfs_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path)) {
                _entries[i].uid = uid;
                _entries[i].gid = gid;

                if (_entries[i].target)
                    (void) _entries[i].target->chown(uid, gid);

                return 0;
            }
        }

        return -ENOENT;
    }
} // namespace astia::fs::filesystems
