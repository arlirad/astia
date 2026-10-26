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
            fs::file* terminal = proc::controlling_terminal();
            if (terminal == nullptr) return {};
            return mm::share<open_file>(mm::shptr<file>::borrowed(terminal), flags);
        }
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path)) {
                const file_mode mode = entries_[i].object->mode();
                // OPENAT uses bit 0 for write-only and bit 1 for read/write.
                if ((flags & 2u) != 0 && (!mode.other_allows(permission_bits::read) || !mode.other_allows(permission_bits::write))) return {};
                if ((flags & 2u) == 0 && (flags & 1u) != 0 && !mode.other_allows(permission_bits::write))
                    return {};
                if ((flags & 3u) == 0 && !mode.other_allows(permission_bits::read)) return {};
                return mm::share<open_file>(entries_[i].object, flags);
            }
        }
        return {};
    }

    file_type devfs_mount::node_type(const char* path) const {
        // The node exists independently of whether the caller has a ctty;
        // only opening it requires a controlling terminal.
        if (equal(path, "/tty")) return directory_chr;
        for (u32 i = 0; i < devfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) return directory_chr;
        return directory_unknown;
    }

    file_mode devfs_mount::node_mode(const char* path) const {
        if (equal(path, "/tty")) return file_mode(file_type::character, file_permissions(0666));
        for (u32 i = 0; i < devfs_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) return entries_[i].object->mode();
        return {};
    }

    u32 devfs_mount::node_uid(const char*) const { return 0; }
    u32 devfs_mount::node_gid(const char*) const { return 0; }

    fs::mount* devfs::mount(const char* source) const {
        (void) source;
        return instance_;
    }

} // namespace astia::fs
