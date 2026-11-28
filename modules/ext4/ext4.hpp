#pragma once

#include "ext4_dir.hpp"
#include "ext4_disk.hpp"
#include "ext4_extent.hpp"
#include "ext4_space.hpp"

#include <astia/core/spinlock.hpp>
#include <astia/fs/module.hpp>
#include <astia/fs/open_file.hpp>
#include <astia/mm/share.hpp>
#include <astia/mm/shptr.hpp>

namespace astia::ext4 {
    class mount;

    class file final : public fs::file,
                       public fs::readable,
                       public fs::writable,
                       public fs::truncatable,
                       public fs::syncable {
        public:
        file(mount* owner, const inode_record& node) : _owner(owner), _node(node) {}

        optional<fs::readable&> as_readable() override {
            return *this;
        }

        optional<fs::writable&> as_writable() override {
            return *this;
        }

        optional<fs::truncatable&> as_truncatable() override {
            return *this;
        }

        optional<fs::syncable&> as_syncable() override {
            return *this;
        }

        core::task<i64> readv(span<const fs::buffer> buffers, u64 offset,
                              core::cltoken token) override;
        core::task<i64> writev(span<const fs::buffer> buffers, u64 offset,
                               core::cltoken token, bool nonblock) override;
        long            size() const override;
        fs::file_mode   mode() const override;

        uid_t uid() const override {
            return uid_t(_node.uid());
        }

        gid_t gid() const override {
            return gid_t(_node.gid());
        }

        u64 inode() const override {
            return _node.number;
        }

        u32 nlink() const override {
            return _node.links_count();
        }

        core::task<i64> truncate(u64 length, core::cltoken token) override;
        core::task<i64> sync(core::cltoken token) override;
        long            chmod(fs::file_permissions permissions) override;
        long            chown(uid_t uid, gid_t gid) override;
        long            touch(const fs::file_timestamps& times) override;

        private:
        long read_ready(void* buffer, u32 length, u64 offset) override;
        long write_ready(const void* buffer, u32 length, u64 offset) override;

        mount*       _owner;
        inode_record _node;
    };

    class mount final : public fs::mount {
        public:
        explicit mount(const char* source);
        ~mount() override;

        bool                     initialize();
        mm::shptr<fs::open_file> open(const char* path, u32 flags) override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            fs::file_type* type, u64* inode) override;
        fs::file_type node_type(const char* path) const override;
        fs::file_mode node_mode(const char* path) const override;
        u64           node_inode(const char* path) const override;
        uid_t         node_uid(const char* path) const override;
        gid_t         node_gid(const char* path) const override;
        fs::file_timestamps node_timestamps(const char* path) const override;
        long                readlink(const char* path, char* buffer,
                                     u32 capacity) const override;

        bool read_only() const override {
            return !_writable;
        }

        bool sync();

        long register_file(const char*, const mm::shptr<fs::file>&) override {
            return -EROFS;
        }

        long mkdir(const char* path, fs::file_mode mode) override;
        long unlink(const char* path, bool directory) override;
        long rename(const char* old_path, const char* new_path) override;
        long link(const char* old_path, const char* new_path) override;
        long chmod(const char* path, fs::file_permissions permissions) override;
        long chown(const char* path, uid_t uid, gid_t gid) override;
        long symlink(const char* target, const char* path) override;

        long read_file(const inode_record& node, void* buffer, u32 length,
                       u64 offset);
        long write_file(inode_record& node, const void* buffer, u32 length,
                        u64 offset);
        long truncate_file(inode_record& node, u64 size);
        long chmod_node(inode_record& node, fs::file_permissions permissions);
        long chown_node(inode_record& node, uid_t uid, gid_t gid);
        long touch_node(inode_record& node, const fs::file_timestamps& times);

        private:
        friend class file;

        static bool read_source(void* context, u64 offset, void* buffer,
                                usz length);
        static bool write_source(void* context, u64 offset, const void* buffer,
                                 usz length);
        bool        source_read(u64 offset, void* buffer, usz length) const;
        bool        source_write(u64 offset, const void* buffer, usz length) const;

        static bool split_parent(const char* path, char* parent, u32 parent_capacity,
                                 char* name, u32 name_capacity);
        bool        lookup(const char* path, inode_record& node) const;
        long resolve_parent(const char* path, inode_record& parent,
                            char name[256]) const;
        bool parent_lookup(const char* path, inode_record& parent, char* name) const;

        long do_read_file(const inode_record& node, void* buffer, u32 length,
                          u64 offset) const;
        long do_write_file(inode_record& node, const void* buffer, u32 length,
                           u64 offset);
        long do_truncate_file(inode_record& node, u64 size);
        long do_chmod_node(inode_record& node, fs::file_permissions permissions);
        long do_chown_node(inode_record& node, uid_t uid, gid_t gid);
        long do_touch_node(inode_record& node, const fs::file_timestamps& times);
        long remove_entry(inode_record& parent, const char* name,
                          inode_record& node);
        long rewrite_parent_entry(inode_record& directory, u32 parent);
        bool descends_from(const inode_record& ancestor,
                           const inode_record& node) const;

        static fs::file_type vfs_type(ext4::node_type type);
        static fs::file_mode vfs_mode(const inode_record& node);

        char _source_path[fs::path_max]{};

        mm::shptr<fs::open_file> _source;
        mutable volume           _volume;
        space                    _space{_volume};
        inode_record             _root;

        mutable core::spinlock _lock;
        bool                   _writable = false;
    };

    class filesystem final : public fs::filesystem {
        public:
        const char* name() const override {
            return "ext4";
        }

        fs::mount* mount(const char* source) const override;

        bool owns_mount() const override {
            return true;
        }

        void destroy_mount(fs::mount* value) const override {
            delete static_cast<ext4::mount*>(value);
        }
    };

    filesystem& type();

    bool register_filesystem();
    void unregister_filesystem();
} // namespace astia::ext4
