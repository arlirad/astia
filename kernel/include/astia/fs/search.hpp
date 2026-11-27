#pragma once

#include <astia/api/errno.h>
#include <astia/common.hpp>

#include "types.hpp"

namespace astia::fs {
    /// Decides whether the calling credentials may search a directory while a
    /// pathname is being resolved.  Implementations own the credential and
    /// node-metadata lookup so the VFS stays independent of proc.
    class search_permission {
        public:
        virtual ~search_permission()                     = default;
        virtual bool allows(const char* directory) const = 0;
    };

    /// Require search permission on every directory prefix of an absolute
    /// canonical path at or below root.  The root anchor "/" itself is not
    /// checked and the final component is skipped unless include_final is set.
    inline long check_search(const char* root, const char* path,
                             const search_permission* search,
                             bool                     include_final = false) {
        if (search == nullptr)
            return 0;

        if (root == nullptr || root[0] != '/' || path == nullptr || path[0] != '/')
            return -EINVAL;

        usize root_length = 0;

        while (root[root_length] != '\0') {
            ++root_length;
        }

        if (root_length + 1 > path_max)
            return -ENAMETOOLONG;

        if (root_length > 1 && root[root_length - 1] == '/')
            return -EINVAL;

        usize length = 0;

        while (path[length] != '\0') {
            ++length;
        }

        if (length + 1 > path_max)
            return -ENAMETOOLONG;

        if (root_length > 1) {
            for (usize i = 0; i < root_length; ++i) {
                if (path[i] != root[i])
                    return -EINVAL;
            }

            if (path[root_length] != '\0' && path[root_length] != '/')
                return -EINVAL;
        }

        char prefix[path_max]{};

        for (usize end = 1; end <= length; ++end) {
            if (end != length && path[end] != '/')
                continue;

            if (end < root_length)
                continue;

            if (end == 1)
                continue;

            if (end == length && !include_final)
                continue;

            usize cut = end;

            while (cut > 1 && path[cut - 1] == '/') {
                --cut;
            }

            if (cut <= 1)
                continue;

            for (usize i = 0; i < cut; ++i) {
                prefix[i] = path[i];
            }

            prefix[cut] = '\0';

            if (!search->allows(prefix))
                return -EACCES;
        }

        return 0;
    }

    inline long check_search(const char* path, const search_permission* search,
                             bool include_final = false) {
        return check_search("/", path, search, include_final);
    }
} // namespace astia::fs
