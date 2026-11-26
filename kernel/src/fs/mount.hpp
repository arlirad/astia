#pragma once

#include <astia/atomic.hpp>
#include <astia/fs.hpp>

namespace astia::fs {
    /// A mounted filesystem instance.  The filesystem type is the
    /// factory; this object carries the mounted state and path lookup.
    class mount {
        public:
        virtual ~mount() = default;

        /// True for an immutable mount: create/write namespace operations
        /// must fail with -EROFS instead of silently looking absent.
        virtual bool read_only() const {
            return false;
        }

        virtual mm::shptr<open_file> open(const char* path, u32 flags);
        virtual long register_file(const char* path, const mm::shptr<file>& entry);
        virtual long unregister_file(const char* path, const mm::shptr<file>& entry = {});
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
            astia::atomic_ref<u32>(_open_count)
                .fetch_add(1u, astia::memory_order::acq_rel);
        }

        void release_open() {
            u32 count =
                astia::atomic_ref<u32>(_open_count).load(astia::memory_order::acquire);

            while (count != 0 && !astia::atomic_ref<u32>(_open_count)
                                      .compare_exchange_strong(
                                          count, count - 1, astia::memory_order::acq_rel,
                                          astia::memory_order::acquire)) {
            }
        }

        u32 open_count() const {
            return astia::atomic_ref<u32>(_open_count).load(astia::memory_order::acquire);
        }

        private:
        mutable u32 _open_count = 0;
    };
} // namespace astia::fs
