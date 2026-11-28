#pragma once

#include <astia/core/spinlock.hpp>
#include <astia/mm/phys_region.hpp>

#include "../../filesystem.hpp"

namespace astia::fs::filesystems {
    /// Entry table a tmpfs mount starts with, matching the production node
    /// budget.  A mount may ask for more through its "nodes=<count>" source.
    inline constexpr u32 tmpfs_default_entries = max_nodes;
    /// Largest entry table a mount may ask for.  The table is only heap
    /// allocated above the default budget.
    inline constexpr u32 tmpfs_max_entries = max_nodes_limit;
    inline constexpr usz tmpfs_max_pages   = 16;

    class tmpfs_file final : public file,
                             public readable,
                             public writable,
                             public mappable,
                             public truncatable,
                             public syncable {
        public:
        explicit tmpfs_file(usz       pages = tmpfs_max_pages,
                            file_mode mode  = file_mode(file_type::regular,
                                                        default_file_permissions));
        ~tmpfs_file();

        optional<readable&> as_readable() override {
            return *this;
        }

        optional<writable&> as_writable() override {
            return *this;
        }

        optional<mappable&> as_mappable() override {
            return *this;
        }

        optional<truncatable&> as_truncatable() override {
            return *this;
        }

        optional<syncable&> as_syncable() override {
            return *this;
        }

        core::task<i64> readv(span<const buffer> buffers, u64 offset,
                              core::cltoken token) override;
        core::task<i64> writev(span<const buffer> buffers, u64 offset,
                               core::cltoken token, bool nonblock) override;
        core::task<i64> sync(core::cltoken token) override;
        core::task<i64> sync_mapping(mm::phys_region* mapping, u64 offset, u64 length,
                                     u32 flags, core::cltoken token) override;
        long            size() const override;

        u64 inode() const override {
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
            advance_times(false, false, true);
            return 0;
        }

        long chown(uid_t owner, gid_t group) override {
            core::spinlock_guard guard(_lock);
            _uid = owner;
            _gid = group;
            advance_times(false, false, true);
            return 0;
        }

        u64 mtime_sec() const override {
            core::spinlock_guard guard(_lock);
            return _times.mtime_sec;
        }

        u64 mtime_nsec() const override {
            core::spinlock_guard guard(_lock);
            return _times.mtime_nsec;
        }

        file_timestamps timestamps() const override {
            core::spinlock_guard guard(_lock);
            return _times;
        }

        long touch(const file_timestamps& times) override;

        void set_mode(file_mode mode) {
            core::spinlock_guard guard(_lock);

            _mode = mode;

            advance_times(false, false, true);
        }

        mm::phys_region* memory_region() override {
            return _storage;
        }

        core::task<i64> truncate(u64 length, core::cltoken token) override;

        private:
        friend class tmpfs_mount;

        long read_ready(void* buffer, u32 length, u64 offset) override;
        long write_ready(const void* buffer, u32 length, u64 offset) override;
        long truncate_ready(u64 length);
        void advance_times(bool atime, bool mtime, bool ctime);

        mm::phys_region* _storage = nullptr;
        u64              _size    = 0;
        file_mode        _mode = file_mode(file_type::regular, default_file_permissions);
        uid_t            _uid{};
        gid_t            _gid{};
        file_timestamps  _times = current_timestamps();
        u64              _inode = 0;
        u32              _nlink = 1;
        // File lock is always acquired after a mount namespace lock.
        mutable core::spinlock _lock;
    };

    class tmpfs_mount final : public mount {
        public:
        tmpfs_mount();
        explicit tmpfs_mount(u32 capacity);
        ~tmpfs_mount() override;

        mm::shptr<open_file> open(const char* path, u32 flags,
                                  long* error = nullptr) override;
        long      open_shared(const char* path, u32 flags, file_mode mode, uid_t uid,
                              gid_t gid, uid_t euid, gid_t egid, bool privileged,
                              mm::shptr<open_file>& result);
        long      unlink_shared(const char* path);
        long      read_directory(const char* path, u64 index, char* name, u32 capacity,
                                 file_type* type, u64* inode) override;
        long      mkdir(const char* path, file_mode mode) override;
        long      unlink(const char* path, bool directory) override;
        long      rename(const char* old_path, const char* new_path) override;
        long      link(const char* old_path, const char* new_path) override;
        file_type node_type(const char* path) const override;
        file_mode node_mode(const char* path) const override;
        u64       node_inode(const char* path) const override;
        long      node_path(u64 inode, char* buffer, u32 capacity) const override;
        uid_t     node_uid(const char* path) const override;
        gid_t     node_gid(const char* path) const override;
        file_timestamps node_timestamps(const char* path) const override;
        long            chmod(const char* path, file_permissions permissions) override;
        long            chown(const char* path, uid_t uid, gid_t gid) override;
        long            symlink(const char* target, const char* path) override;
        long readlink(const char* path, char* buffer, u32 capacity) const override;

        private:
        file_type node_type_unlocked(const char* path) const;

        struct entry {
            bool                  used      = false;
            bool                  directory = false;
            file_mode             mode{};
            uid_t                 uid{};
            gid_t                 gid{};
            char                  path[path_max]{};
            mm::shptr<tmpfs_file> target;
        };

        entry _inline_entries[tmpfs_default_entries]{};
        // A mount that asks for more than the default budget swaps this
        // pointer to a heap array sized to its request.  Both fields keep
        // zero-initialised defaults: the kernel runs no global constructors,
        // so a static mount (the shared-memory one) only ever sees the zero
        // image and must still work.
        entry* _entries  = nullptr;
        u32    _capacity = 0;

        entry* entry_table() {
            return _entries != nullptr ? _entries : _inline_entries;
        }

        const entry* entry_table() const {
            return _entries != nullptr ? _entries : _inline_entries;
        }

        u32 node_capacity() const {
            return _capacity != 0 ? _capacity : tmpfs_default_entries;
        }

        // The mount root has no entry; stamp it on first lookup once a clock
        // exists so stat("/tmp") reports a stable creation time.
        mutable file_timestamps _root_times{};
        // rename/link run entirely under _namespace_lock and share this
        // scratch so their 4096-byte working path stays off the kernel stack.
        char _path_scratch[path_max]{};
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

        bool owns_mount() const override {
            return true;
        }

        void destroy_mount(fs::mount* value) const override {
            delete static_cast<tmpfs_mount*>(value);
        }
    };
} // namespace astia::fs::filesystems
