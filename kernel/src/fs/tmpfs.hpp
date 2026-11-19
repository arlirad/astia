#pragma once

#include "filesystem.hpp"
#include <astia/core/spinlock.hpp>
#include <astia/mm/phys_region.hpp>

namespace astia::fs {

    inline constexpr u32   tmpfs_max_entries = 32;
    inline constexpr usize tmpfs_max_pages   = 16;

    class tmpfs_file final : public file {
        public:
        explicit tmpfs_file(usize     pages = tmpfs_max_pages,
                            file_mode mode  = file_mode(file_type::regular,
                                                        default_file_permissions));
        ~tmpfs_file();

        long        read(void* buffer, u32 length, u64 offset) override;
        long        write(const void* buffer, u32 length, u64 offset) override;
        long        sync() override;
        long        sync_mapping(mm::phys_region* mapping, u64 offset, u64 length,
                                 u32 flags) override;
        long        size() const override;
        const void* capability(file_capability kind) const override;
        u64         inode() const override {
            core::spinlock_guard guard(_lock);
            return _inode;
        }
        u32 nlink() const override {
            core::spinlock_guard guard(_lock);
            return _nlink;
        }
        void add_link() {
            core::spinlock_guard guard(_lock);
            ++_nlink;
        }
        void drop_link() {
            core::spinlock_guard guard(_lock);
            if (_nlink != 0)
                --_nlink;
        }
        file_mode mode() const override {
            core::spinlock_guard guard(_lock);
            return _mode;
        }
        uid_t uid() const override {
            core::spinlock_guard guard(_lock);
            return _uid;
        }
        gid_t gid() const override {
            core::spinlock_guard guard(_lock);
            return _gid;
        }
        long chmod(file_permissions permissions) override {
            core::spinlock_guard guard(_lock);
            _mode = _mode.chmod(permissions);
            return 0;
        }
        long chown(uid_t owner, gid_t group) override {
            core::spinlock_guard guard(_lock);
            _uid = owner;
            _gid = group;
            return 0;
        }
        u64 mtime_sec() const override {
            core::spinlock_guard guard(_lock);
            return _mtime_sec;
        }
        u64 mtime_nsec() const override {
            core::spinlock_guard guard(_lock);
            return _mtime_nsec;
        }
        long touch(u64 sec, u64 nsec) override;
        void set_mode(file_mode mode) {
            core::spinlock_guard guard(_lock);
            _mode = mode;
        }
        mm::phys_region* memory_region() override {
            return _storage;
        }
        long truncate(u64 length) override;

        private:
        mm::phys_region* _storage = nullptr;
        u64              _size    = 0;
        file_mode        _mode = file_mode(file_type::regular, default_file_permissions);
        uid_t            _uid{};
        gid_t            _gid{};
        u64              _mtime_sec  = 0;
        u64              _mtime_nsec = 0;
        u64              _inode      = 0;
        u32              _nlink      = 1;
        // File lock is always acquired after a mount namespace lock.
        mutable core::spinlock _lock;
    };

    class tmpfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        long      open_shared(const char* path, u32 flags, file_mode mode, uid_t uid,
                              gid_t gid, uid_t euid, gid_t egid, bool privileged,
                              mm::shptr<open_file>& result);
        long      unlink_shared(const char* path);
        long      read_directory(const char* path, u64 index, char* name, u32 capacity,
                                 file_type* type) override;
        long      mkdir(const char* path, file_mode mode) override;
        long      unlink(const char* path, bool directory) override;
        long      rename(const char* old_path, const char* new_path) override;
        long      link(const char* old_path, const char* new_path) override;
        file_type node_type(const char* path) const override;
        file_mode node_mode(const char* path) const override;
        uid_t     node_uid(const char* path) const override;
        gid_t     node_gid(const char* path) const override;
        long      chmod(const char* path, file_permissions permissions) override;
        long      chown(const char* path, uid_t uid, gid_t gid) override;
        long      symlink(const char* target, const char* path) override;
        long      readlink(const char* path, char* buffer, u32 capacity) const override;

        private:
        file_type node_type_unlocked(const char* path) const;
        struct entry {
            bool                  used      = false;
            bool                  directory = false;
            file_mode             mode{};
            uid_t                 uid{};
            gid_t                 gid{};
            char                  path[path_max]{};
            mm::shptr<tmpfs_file> object;
        };
        entry _entries[tmpfs_max_entries]{};
        // Namespace lock protects entry publication and path identity.
        // Callers may then acquire a file lock, never the reverse.
        mutable core::spinlock _namespace_lock;
    };

    class tmpfs final : public filesystem {
        public:
        const char* name() const override {
            return "tmpfs";
        }
        fs::mount* mount(const char* source) const override;
        bool       owns_mount() const override {
            return true;
        }
        void destroy_mount(fs::mount* value) const override {
            delete static_cast<tmpfs_mount*>(value);
        }
    };

    long open_shared_memory(const char* path, u32 flags, file_mode mode, uid_t uid,
                            gid_t gid, uid_t euid, gid_t egid, bool privileged,
                            mm::shptr<open_file>& result);
    long unlink_shared_memory(const char* path);

} // namespace astia::fs
