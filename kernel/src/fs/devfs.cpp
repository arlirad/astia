#include "devfs.hpp"

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
        core::spinlock_guard guard(namespace_lock_);
        if (!object || path == nullptr || path[0] != '/')
            return -EINVAL;
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path))
                return -EEXIST;
        }
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (!entries_[i].used) {
                if (!copy_path(entries_[i].path, path))
                    return -ENAMETOOLONG;
                entries_[i].used   = true;
                entries_[i].object = object;
                return 0;
            }
        }
        return -ENFILE;
    }

    mm::shptr<open_file> devfs_mount::open(const char* path, u32 flags) {
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
            core::spinlock_guard guard(namespace_lock_);
            for (u32 i = 0; i < devfs_max_entries; ++i)
                if (entries_[i].used && equal(entries_[i].path, path)) {
                    object = entries_[i].object;
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
        core::spinlock_guard guard(namespace_lock_);
        // The node exists independently of whether the caller has a ctty;
        // only opening it requires a controlling terminal.
        if (equal(path, "/tty"))
            return directory_chr;
        for (u32 i = 0; i < devfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path))
                return directory_chr;
        return directory_unknown;
    }

    file_mode devfs_mount::node_mode(const char* path) const {
        if (equal(path, "/tty"))
            return file_mode(file_type::character, default_device_permissions);
        mm::shptr<file> object;
        {
            core::spinlock_guard guard(namespace_lock_);
            for (u32 i = 0; i < devfs_max_entries; ++i)
                if (entries_[i].used && equal(entries_[i].path, path)) {
                    object = entries_[i].object;
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
        return instance_;
    }

} // namespace astia::fs
