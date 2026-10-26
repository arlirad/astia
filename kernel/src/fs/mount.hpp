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
        virtual long mkdir(const char* path, u32 mode);
        virtual long unlink(const char* path, bool directory);
        virtual long rename(const char* old_path, const char* new_path);
        virtual u32 node_type(const char* path) const;
        virtual u32 node_mode(const char* path) const;
        virtual u32 node_uid(const char* path) const;
        virtual u32 node_gid(const char* path) const;
        virtual long chmod(const char* path, u32 mode);
        virtual long symlink(const char* target, const char* path);
        virtual long readlink(const char* path, char* buffer, u32 capacity) const;
    };

} // namespace astia::fs
