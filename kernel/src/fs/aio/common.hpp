#pragma once

#include <astia/api/errno.h>

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
} // namespace astia::io::aio::operations
