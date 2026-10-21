#pragma once

#include <astia/fs.hpp>

namespace astia::fs {

    /// A mounted filesystem instance.  The filesystem type is the
    /// factory; this object carries the mounted state and path lookup.
    class mount {
        public:
        virtual mm::shptr<open_file> open(const char* path, u32 flags);
        virtual long register_file(const char* path, const mm::shptr<file>& object);
        virtual long read_directory(const char* path, u64 index, char* name, u32 capacity,
                                    u32* type);
    };

} // namespace astia::fs
