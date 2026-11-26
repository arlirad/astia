#pragma once

#include <astia/api/errno.h>
#include <astia/strings.hpp>

#include "tmpfs.hpp"

namespace astia::fs::filesystems::tmpfs_detail {
    inline constexpr u32 oflag_write = ASTIA_AIO_O_WRONLY;
    inline constexpr u32 oflag_rdwr  = ASTIA_AIO_O_RDWR;
    inline constexpr u32 oflag_creat = ASTIA_AIO_O_CREAT;
    inline constexpr u32 oflag_excl  = ASTIA_AIO_O_EXCL;
    inline constexpr u32 oflag_trunc = ASTIA_AIO_O_TRUNC;

    inline bool copy_path(char* dst, const char* src) {
        if (src == nullptr || src[0] != '/')
            return false;

        u32 i = 0;

        for (; src[i] != '\0' && i + 1 < path_max; ++i) {
            dst[i] = src[i];
        }

        if (src[i] != '\0')
            return false;

        dst[i] = '\0';

        return true;
    }

    inline bool parent_of(const char* parent, const char* path) {
        u32 n = 0;

        while (parent[n] != '\0') {
            ++n;
        }

        if (n == 1)
            return path[0] == '/';

        for (u32 i = 0; i < n; ++i) {
            if (parent[i] != path[i])
                return false;
        }

        return path[n] == '/';
    }

    inline bool access_allowed(file_mode mode, uid_t owner, gid_t group, uid_t euid,
                               gid_t egid, permission_bits want, bool privileged) {
        if (privileged)
            return want != permission_bits::execute ||
                   mode.owner_allows(permission_bits::execute) ||
                   mode.group_allows(permission_bits::execute) ||
                   mode.other_allows(permission_bits::execute);

        if (euid == owner)
            return mode.owner_allows(want);

        if (egid == group)
            return mode.group_allows(want);

        return mode.other_allows(want);
    }
} // namespace astia::fs::filesystems::tmpfs_detail
