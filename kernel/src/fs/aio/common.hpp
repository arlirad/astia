#pragma once

#include <astia/api/errno.h>
#include <astia/fs/open_file.hpp>

#include "../../io/aio/dispatch.hpp"
#include "../../io/aio/memory.hpp"
#include "../../io/aio/ring.hpp"
#include "../../proc/mapping.hpp"
#include "../../proc/proc.hpp"
#include "../mapping.hpp"

namespace astia::io::aio::operations {
    using namespace ::astia::io::aio::ring;
    using ::astia::io::aio::dispatch::directory_listable;
    using ::astia::io::aio::dispatch::file_for;
    using ::astia::io::aio::dispatch::owner_or_writable;
    using ::astia::io::aio::dispatch::parent_searchable;
    using ::astia::io::aio::dispatch::parent_writable;
    using ::astia::io::aio::dispatch::resolve_at;
    using ::astia::io::aio::dispatch::resolve_path_for;
    using ::astia::io::aio::dispatch::sticky_directory_allows;

    /// Copy a caller string into fixed kernel storage.  A missing terminator
    /// is only ENAMETOOLONG when the bounded source range is actually mapped;
    /// otherwise the failure is a bad pointer.
    inline long copy_user_string(char* destination, usz capacity,
                                 memory::user_ptr<char> source) {
        const span<char> buffer(destination, capacity);

        if (memory::copy_in_string(buffer, source))
            return 0;

        return source.validate(capacity) ? -ENAMETOOLONG : -EFAULT;
    }

    inline const char* path_only_path(const fs::open_file& handle) {
        return handle.parent() ? nullptr : handle.path();
    }

    inline uid_t handle_uid(const fs::open_file& handle) {
        if (handle.parent())
            return handle.parent()->uid();

        return handle.path() != nullptr ? fs::node_uid(handle.path()) : uid_t{};
    }

    inline gid_t handle_gid(const fs::open_file& handle) {
        if (handle.parent())
            return handle.parent()->gid();

        return handle.path() != nullptr ? fs::node_gid(handle.path()) : gid_t{};
    }

    inline u32 handle_nlink(const fs::open_file& handle) {
        return handle.parent() ? handle.parent()->nlink() : 1u;
    }
} // namespace astia::io::aio::operations
