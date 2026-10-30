#pragma once

#include "filesystem.hpp"
#include <astia/mm/phys_region.hpp>

namespace astia::fs {

    inline constexpr u32   tmpfs_max_entries = 32;
    inline constexpr usize tmpfs_max_pages   = 16;

    class tmpfs_file final : public file {
        public:
        explicit tmpfs_file(usize pages = tmpfs_max_pages, file_mode mode = file_mode(file_type::regular, file_permissions(0666)));
        ~tmpfs_file();

        long             read(void* buffer, u32 length, u64 offset) override;
        long             write(const void* buffer, u32 length, u64 offset) override;
        long             size() const override;
        const void*      capability(file_capability kind) const override;
        u64              inode() const override { return inode_; }
        u32              nlink() const override { return nlink_; }
        void             add_link() { ++nlink_; }
        void             drop_link() { if (nlink_ != 0) --nlink_; }
        file_mode        mode() const override { return mode_; }
        uid_t            uid() const override { return uid_; }
        gid_t            gid() const override { return gid_; }
        long             chown(uid_t owner, gid_t group) override { uid_ = owner; gid_ = group; return 0; }
        u64              mtime_sec() const override { return mtime_sec_; }
        u64              mtime_nsec() const override { return mtime_nsec_; }
        long             touch(u64 sec, u64 nsec) override;
        void             set_mode(file_mode mode) { mode_ = mode; }
        mm::phys_region* memory_region() override {
            return storage_;
        }
        void truncate() {
            size_ = 0;
        }

        private:
        mm::phys_region* storage_ = nullptr;
        u64              size_    = 0;
        file_mode        mode_    = file_mode(file_type::regular, file_permissions(0666));
        uid_t            uid_{}; gid_t gid_{};
        u64              mtime_sec_ = 0;
        u64              mtime_nsec_ = 0;
        u64              inode_ = 0;
        u32              nlink_ = 1;
    };

    class tmpfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            file_type* type) override;
        long mkdir(const char* path, file_mode mode) override;
        long unlink(const char* path, bool directory) override;
        long rename(const char* old_path, const char* new_path) override;
        long link(const char* old_path, const char* new_path) override;
        file_type node_type(const char* path) const override;
        file_mode node_mode(const char* path) const override;
        uid_t node_uid(const char* path) const override;
        gid_t node_gid(const char* path) const override;
        long chmod(const char* path, file_permissions permissions) override;
        long chown(const char* path, uid_t uid, gid_t gid) override;
        long symlink(const char* target, const char* path) override;
        long readlink(const char* path, char* buffer, u32 capacity) const override;

        private:
        struct entry {
            bool                  used = false;
            bool                  directory = false;
            file_mode             mode{};
            uid_t                 uid{}; gid_t gid{};
            char                  path[path_max]{};
            mm::shptr<tmpfs_file> object;
        };
        entry entries_[tmpfs_max_entries]{};
    };

    class tmpfs final : public filesystem {
        public:
        const char* name() const override {
            return "tmpfs";
        }
        fs::mount* mount(const char* source) const override;

    };

} // namespace astia::fs
