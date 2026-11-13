#pragma once

#include "../filesystem.hpp"
#include "ext4_reader.hpp"

namespace astia::fs {

    class ext4_mount;

    class ext4_file final : public file {
        public:
        ext4_file(ext4_mount* owner, const ext4::inode& node) : owner_(owner), node_(node) {}

        long read(void* buffer, u32 length, u64 offset) override;
        long write(const void*, u32, u64) override {
            return -EROFS;
        }
        long size() const override;
        file_mode mode() const override;
        uid_t uid() const override {
            return uid_t(node_.uid);
        }
        gid_t gid() const override {
            return gid_t(node_.gid);
        }
        u64 inode() const override {
            return node_.number;
        }
        u32 nlink() const override {
            return node_.links;
        }
        long truncate(u64) override {
            return -EROFS;
        }
        long chmod(file_permissions) override {
            return -EROFS;
        }
        long chown(uid_t, gid_t) override {
            return -EROFS;
        }
        long touch(u64, u64) override {
            return -EROFS;
        }
        long sync_mapping(mm::phys_region*, u64, u64, u32) override {
            return -EROFS;
        }

        const ext4::inode& on_disk_inode() const {
            return node_;
        }

        private:
        ext4_mount* owner_;
        ext4::inode node_;
    };

    class ext4_mount final : public mount {
        public:
        explicit ext4_mount(const mm::shptr<open_file>& source) : source_(source) {}

        bool initialize();
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            file_type* type) override;
        file_type node_type(const char* path) const override;
        file_mode node_mode(const char* path) const override;
        uid_t node_uid(const char* path) const override;
        gid_t node_gid(const char* path) const override;
        long register_file(const char*, const mm::shptr<file>&) override {
            return -EROFS;
        }
        long mkdir(const char*, file_mode) override {
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
        long chmod(const char*, file_permissions) override {
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
        friend class ext4_file;
        static bool read_source(void* context, u64 offset, void* buffer, usize length);
        bool source_read(u64 offset, void* buffer, usize length) const;
        bool lookup(const char* path, ext4::inode& node) const;
        bool child(const ext4::inode& parent, const char* name, ext4::inode& node) const;
        static file_type vfs_type(ext4::node_type type);
        static file_mode vfs_mode(const ext4::inode& node);

        mm::shptr<open_file> source_;
        mutable ext4::reader reader_;
        ext4::inode root_;
    };

    class ext4_filesystem final : public filesystem {
        public:
        const char* name() const override {
            return "ext4";
        }
        fs::mount* mount(const char* source) const override;
        bool owns_mount() const override {
            return true;
        }
        void destroy_mount(fs::mount* value) const override {
            delete static_cast<ext4_mount*>(value);
        }
    };

    ext4_filesystem& ext4_type();

} // namespace astia::fs
