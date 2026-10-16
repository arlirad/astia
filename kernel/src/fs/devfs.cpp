#include <astia/api/errno.h>
#include <astia/fs/devfs.hpp>
#include <astia/mm/share.hpp>

namespace astia::fs {

    namespace {
        auto equal(const char* lhs, const char* rhs) -> bool {
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        auto copy_path(char* dst, const char* src) -> bool {
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

    auto devfs_mount::register_file(const char* path, const mm::shptr<file>& object)
        -> long {
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

    auto devfs_mount::open(const char* path, u32 flags) -> mm::shptr<open_file> {
        for (u32 i = 0; i < devfs_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path)) {
                return mm::share<open_file>(entries_[i].object, flags);
            }
        }
        return {};
    }

    auto devfs::mount(const char* source) const -> fs::mount* {
        (void) source;
        return instance_;
    }

} // namespace astia::fs
