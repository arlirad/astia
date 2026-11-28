#pragma once

#include <astia/atomic.hpp>
#include <astia/fs.hpp>

namespace astia::fs {
    namespace detail {
        /// Mount identity for st_dev.  Registry slots are recycled by
        /// unmount, so the slot index cannot name a mount across time; a
        /// monotonic counter identifies the instance for its whole life.
        inline u32 allocate_mount_device() {
            static u32 next = 1;

            return astia::atomic_ref<u32>(next).fetch_add(1u,
                                                          astia::memory_order::relaxed);
        }
    } // namespace detail

    /// A mounted filesystem instance.  The filesystem type is the
    /// factory; this object carries the mounted state and path lookup.
    class mount {
        public:
        mount() : _device(detail::allocate_mount_device()) {}

        virtual ~mount() = default;

        /// Identity the mount reports as st_dev: nonzero and unique among
        /// live mounts, and shared by every object the mount backs.
        u32 device() const {
            return _device;
        }

        /// True for an immutable mount: create/write namespace operations
        /// must fail with -EROFS instead of silently looking absent.
        virtual bool read_only() const {
            return false;
        }

        /// Open `path`.  A filesystem that cannot satisfy a create reports
        /// why through `error` (for example -ENOSPC for a full node table)
        /// instead of leaving the caller to guess from an empty handle.
        virtual mm::shptr<open_file> open(const char* path, u32 flags,
                                          long* error = nullptr);
        /// Open `path`, binding only the node whose inode is `inode` (zero
        /// when the mount cannot identify one).  Backends that resolve and
        /// open under one namespace lock override this so a name that changed
        /// since the caller checked it fails with -EAGAIN instead of binding
        /// the replacement.
        virtual mm::shptr<open_file> open_verified(const char* path, u32 flags, u64 inode,
                                                   long* error = nullptr);
        virtual long register_file(const char* path, const mm::shptr<file>& entry);
        virtual long unregister_file(const char* path, const mm::shptr<file>& entry = {});
        virtual long read_directory(const char* path, u64 index, char* name, u32 capacity,
                                    file_type* type, u64* inode);
        virtual long mkdir(const char* path, file_mode mode);
        virtual long unlink(const char* path, bool directory);
        virtual long rename(const char* old_path, const char* new_path);
        virtual long link(const char* old_path, const char* new_path);
        virtual file_type node_type(const char* path) const;
        virtual file_mode node_mode(const char* path) const;
        virtual u64       node_inode(const char* path) const;
        /// Path of the node identified by `inode` inside this mount, or
        /// -ENOENT when the filesystem has no live name for it.  The result
        /// is mount-relative and always starts with '/'.
        virtual long            node_path(u64 inode, char* buffer, u32 capacity) const;
        virtual uid_t           node_uid(const char* path) const;
        virtual gid_t           node_gid(const char* path) const;
        virtual file_timestamps node_timestamps(const char* path) const;
        virtual long            chmod(const char* path, file_permissions permissions);
        virtual long            chown(const char* path, uid_t uid, gid_t gid);
        virtual long            symlink(const char* target, const char* path);
        virtual long readlink(const char* path, char* buffer, u32 capacity) const;

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
        const u32   _device;
        mutable u32 _open_count = 0;
    };
} // namespace astia::fs
