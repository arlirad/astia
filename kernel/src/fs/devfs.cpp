#include "devfs.hpp"

#include "../dev/chr/tty/pty.hpp"
#include "../proc/proc.hpp"
#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::fs {

    namespace {
        bool equal(const char* lhs, const char* rhs) {
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        bool copy_path(char* dst, const char* src) {
            if (src == nullptr || src[0] != '/')
                return false;
            u32 i = 0;
            for (; src[i] != '\0' && i + 1 < path_max; ++i)
                dst[i] = src[i];
            if (src[i] != '\0')
                return false;
            dst[i] = '\0';
            return true;
        }
    }

    long devfs_mount::register_file(const char* path, const mm::shptr<file>& object) {
        core::spinlock_guard guard(_namespace_lock);
        if (!object || path == nullptr || path[0] != '/')
            return -EINVAL;
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (_entries[i].used && equal(_entries[i].path, path))
                return -EEXIST;
        }
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (!_entries[i].used) {
                if (!copy_path(_entries[i].path, path))
                    return -ENAMETOOLONG;
                _entries[i].used   = true;
                _entries[i].object = object;
                return 0;
            }
        }
        return -ENFILE;
    }

    long devfs_mount::unregister_file(const char* path, const mm::shptr<file>& object) {
        core::spinlock_guard guard(_namespace_lock);
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (!_entries[i].used || !equal(_entries[i].path, path))
                continue;
            if (object && _entries[i].object.get() != object.get())
                return -ENOENT;
            _entries[i].object  = {};
            _entries[i].path[0] = '\0';
            _entries[i].used    = false;
            return 0;
        }
        return -ENOENT;
    }

    mm::shptr<open_file> devfs_mount::open(const char* path, u32 flags) {
        if (equal(path, "/ptmx"))
            return dev::pty_open_master(flags);
        if (dev::pty_node_exists(path))
            return dev::pty_open_slave(path, flags);
        if (equal(path, "/tty")) {
            mm::shptr<open_file> terminal = proc::controlling_terminal();
            if (!terminal)
                return {};
            auto result = mm::share<open_file>(*terminal);
            if (result)
                result->set_status_flags(flags);
            return result;
        }
        mm::shptr<file> object;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < devfs_max_entries; ++i)
                if (_entries[i].used && equal(_entries[i].path, path)) {
                    object = _entries[i].object;
                    break;
                }
        }
        if (object) {
            const file_mode mode = object->mode();
            // OPENAT uses bit 0 for write-only and bit 1 for read/write.
            if ((flags & ASTIA_AIO_O_RDWR) == ASTIA_AIO_O_RDWR &&
                (!mode.other_allows(permission_bits::read) ||
                 !mode.other_allows(permission_bits::write)))
                return {};
            if ((flags & ASTIA_AIO_O_RDWR) != ASTIA_AIO_O_RDWR &&
                (flags & ASTIA_AIO_O_WRONLY) != 0 &&
                !mode.other_allows(permission_bits::write))
                return {};
            if ((flags & ASTIA_AIO_O_ACCMODE) == ASTIA_AIO_O_RDONLY &&
                !mode.other_allows(permission_bits::read))
                return {};
            return mm::share<open_file>(object, flags);
        }
        return {};
    }

    file_type devfs_mount::node_type(const char* path) const {
        core::spinlock_guard guard(_namespace_lock);
        // The node exists independently of whether the caller has a ctty;
        // only opening it requires a controlling terminal.
        if (equal(path, "/tty"))
            return directory_chr;
        if (dev::pty_node_is_directory(path))
            return directory_dir;
        if (dev::pty_node_exists(path) || equal(path, "/ptmx"))
            return directory_chr;
        for (u32 i = 0; i < devfs_max_entries; ++i)
            if (_entries[i].used && equal(_entries[i].path, path))
                return directory_chr;
        return directory_unknown;
    }

    file_mode devfs_mount::node_mode(const char* path) const {
        if (equal(path, "/tty"))
            return file_mode(file_type::character, default_device_permissions);
        if (dev::pty_node_is_directory(path))
            return file_mode(file_type::directory, default_directory_permissions);
        if (dev::pty_node_exists(path) || equal(path, "/ptmx"))
            return file_mode(file_type::character, default_device_permissions);
        mm::shptr<file> object;
        {
            core::spinlock_guard guard(_namespace_lock);
            for (u32 i = 0; i < devfs_max_entries; ++i)
                if (_entries[i].used && equal(_entries[i].path, path)) {
                    object = _entries[i].object;
                    break;
                }
        }
        if (object)
            return object->mode();
        return {};
    }

    uid_t devfs_mount::node_uid(const char*) const {
        return {};
    }
    gid_t devfs_mount::node_gid(const char*) const {
        return {};
    }

    fs::mount* devfs::mount(const char* source) const {
        (void) source;
        return _instance;
    }

} // namespace astia::fs
