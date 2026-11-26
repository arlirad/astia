#pragma once

#include <astia/fs.hpp>

#include "../../proc.hpp"

namespace astia::proc::syscalls::detail {
    inline bool exec_permission_denied(const process& owner, const char* path) {
        const fs::file_mode mode = fs::node_mode(path);

        return mode.exists() &&
               !owner.can_access(mode, fs::node_uid(path), fs::node_gid(path),
                                 fs::permission_bits::execute);
    }
} // namespace astia::proc::syscalls::detail
