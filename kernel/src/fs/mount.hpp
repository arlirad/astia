#pragma once

#include <astia/fs.hpp>

namespace astia::fs {

    /// A mounted filesystem instance.  The filesystem type is the
    /// factory; this object carries the mounted state and path lookup.
    class mount {
        public:
        virtual ~mount() = default;
        virtual mm::shptr<open_file> open(const char* path, u32 flags);
        virtual long register_file(const char* path, const mm::shptr<file>& object);
        virtual long read_directory(const char* path, u64 index, char* name, u32 capacity,
                                    file_type* type);
        virtual long mkdir(const char* path, file_mode mode);
        virtual long unlink(const char* path, bool directory);
        virtual long rename(const char* old_path, const char* new_path);
        virtual long link(const char* old_path, const char* new_path);
        virtual file_type node_type(const char* path) const;
        virtual file_mode node_mode(const char* path) const;
        virtual uid_t     node_uid(const char* path) const;
        virtual gid_t     node_gid(const char* path) const;
        virtual long      chmod(const char* path, file_permissions permissions);
        virtual long      chown(const char* path, uid_t uid, gid_t gid);
        virtual long      symlink(const char* target, const char* path);
        virtual long      readlink(const char* path, char* buffer, u32 capacity) const;

        void retain_open() {
            __atomic_fetch_add(&open_count_, 1u, __ATOMIC_ACQ_REL);
        }
        void release_open() {
            u32 count = __atomic_load_n(&open_count_, __ATOMIC_ACQUIRE);
            while (count != 0 &&
                   !__atomic_compare_exchange_n(&open_count_, &count, count - 1, false,
                                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            }
        }
        u32 open_count() const {
            return __atomic_load_n(&open_count_, __ATOMIC_ACQUIRE);
        }

        private:
        u32 open_count_ = 0;
    };

} // namespace astia::fs
