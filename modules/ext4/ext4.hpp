#pragma once

#include "ext4_reader.hpp"

#include <astia/fs/module.hpp>

namespace astia::ext4 {
    class mount;

    class file final : public fs::file,
                       public fs::readable,
                       public fs::writable,
                       public fs::truncatable,
                       public fs::syncable {
        public:
        file(mount* owner, const ext4::inode& node) : _owner(owner), _node(node) {}

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
            return uid_t(_node.uid);
        }

        gid_t gid() const override {
            return gid_t(_node.gid);
        }

        u64 inode() const override {
            return _node.number;
        }

        u32 nlink() const override {
            return _node.links;
        }

        core::task<i64> truncate(u64, core::cltoken) override {
            co_return -EROFS;
        }

        core::task<i64> sync(core::cltoken) override {
            co_return -EROFS;
        }

        long chmod(fs::file_permissions) override {
            return -EROFS;
        }

        long chown(uid_t, gid_t) override {
            return -EROFS;
        }

        long touch(const fs::file_timestamps&) override {
            return -EROFS;
        }

        const ext4::inode& on_disk_inode() const {
            return _node;
        }

        private:
        long read_ready(void* buffer, u32 length, u64 offset) override;

        long write_ready(const void*, u32, u64) override {
            return -EROFS;
        }

        mount*       _owner;
        ext4::inode _node;
    };

    class mount final : public fs::mount {
        public:
        explicit mount(const mm::shptr<fs::open_file>& source) : _source(source) {}

        bool                         initialize();
        mm::shptr<fs::open_file>     open(const char* path, u32 flags) override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            fs::file_type* type, u64* inode) override;
        fs::file_type node_type(const char* path) const override;
        fs::file_mode node_mode(const char* path) const override;
        u64           node_inode(const char* path) const override;
        uid_t         node_uid(const char* path) const override;
        gid_t         node_gid(const char* path) const override;
        long          readlink(const char* path, char* buffer, u32 capacity) const override;

        long register_file(const char*, const mm::shptr<fs::file>&) override {
            return -EROFS;
        }

        long mkdir(const char*, fs::file_mode) override {
            return -EROFS;
        }

        long unlink(const char*, bool) override {
            return -EROFS;
        }

        long rename(const char*, const char*) override {
            return -EROFS;
        }

        long link(const char*, const char*) override {
            return -EROFS;
        }

        long chmod(const char*, fs::file_permissions) override {
            return -EROFS;
        }

        long chown(const char*, uid_t, gid_t) override {
            return -EROFS;
        }

        long symlink(const char*, const char*) override {
            return -EROFS;
        }

        bool read_file(const ext4::inode& node, void* buffer, u32 length, u64 offset);

        private:
        friend class file;
        static bool read_source(void* context, u64 offset, void* buffer, usz length);
        bool        source_read(u64 offset, void* buffer, usz length) const;
        bool        lookup(const char* path, ext4::inode& node) const;
        bool        child(const ext4::inode& parent, const char* name, ext4::inode& node) const;
        static fs::file_type vfs_type(ext4::node_type type);
        static fs::file_mode vfs_mode(const ext4::inode& node);

        mm::shptr<fs::open_file> _source;
        mutable ext4::reader     _reader;
        ext4::inode              _root;
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
