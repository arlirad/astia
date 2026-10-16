#pragma once

#include <astia/fs/fs.hpp>

namespace astia::fs {

    /// A mounted filesystem instance.  The filesystem type is the
    /// factory; this object carries the mounted state and path lookup.
    class mount {
        public:
        virtual auto open(const char* path, u32 flags) -> mm::shptr<open_file>;
        virtual auto register_file(const char* path, const mm::shptr<file>& object)
            -> long;
    };

} // namespace astia::fs
