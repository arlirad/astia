#pragma once

#include "filesystem.hpp"
#include <astia/mm/phys_region.hpp>

namespace astia::fs {

    inline constexpr u32   tmpfs_max_entries = 32;
    inline constexpr usize tmpfs_max_pages   = 16;

    class tmpfs_file final : public file {
        public:
        explicit tmpfs_file(usize pages = tmpfs_max_pages, u32 mode = 0100666);
        ~tmpfs_file();

        long             read(void* buffer, u32 length, u64 offset) override;
        long             write(const void* buffer, u32 length, u64 offset) override;
        long             size() const override;
        u32              mode() const override { return mode_; }
        u32              uid() const override { return uid_; }
        u32              gid() const override { return gid_; }
        u64              mtime_sec() const override { return mtime_sec_; }
        u64              mtime_nsec() const override { return mtime_nsec_; }
        long             touch(u64 sec, u64 nsec) override;
        void             set_mode(u32 mode) { mode_ = mode; }
        mm::phys_region* memory_region() override {
            return storage_;
        }
        void truncate() {
            size_ = 0;
        }

        private:
        mm::phys_region* storage_ = nullptr;
        u64              size_    = 0;
        u32              mode_    = 0100666;
        u32              uid_ = 0, gid_ = 0;
        u64              mtime_sec_ = 0;
        u64              mtime_nsec_ = 0;
    };

    class tmpfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            u32* type) override;
        long mkdir(const char* path, u32 mode) override;
        long unlink(const char* path, bool directory) override;
        long rename(const char* old_path, const char* new_path) override;
        u32 node_type(const char* path) const override;
        u32 node_mode(const char* path) const override;
        u32 node_uid(const char* path) const override;
        u32 node_gid(const char* path) const override;
        long chmod(const char* path, u32 mode) override;
        long symlink(const char* target, const char* path) override;
        long readlink(const char* path, char* buffer, u32 capacity) const override;

        private:
        struct entry {
            bool                  used = false;
            bool                  directory = false;
            u32                   mode = 0;
            u32                   uid = 0, gid = 0;
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

        private:
        mutable tmpfs_mount* instance_ = nullptr;
    };

} // namespace astia::fs
