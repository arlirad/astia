#pragma once

#include <astia/api/errno.h>
#include <astia/fs.hpp>

#include "process.hpp"
#include "search_permission.hpp"

namespace astia::proc {
    inline bool exec_permission_denied(const process& owner, const char* path) {
        const fs::file_mode mode = fs::node_mode(path);

        return mode.exists() &&
               !owner.can_access(mode, fs::node_uid(path), fs::node_gid(path),
                                 fs::permission_bits::execute);
    }

    inline i64 exec_search_permission(const process& owner, const char* path) {
        char root[fs::path_max]{};

        const i64 root_rc = owner.copy_root(root, sizeof root);
        if (root_rc < 0)
            return root_rc;

        const process_search_permission search(owner);

        return fs::check_search(root, path, &search, false);
    }

    inline i64 exec_path_check(const process& owner, const char* path) {
        const i64 search_rc = exec_search_permission(owner, path);
        if (search_rc < 0)
            return search_rc;

        return exec_permission_denied(owner, path) ? -EACCES : 0;
    }
} // namespace astia::proc
