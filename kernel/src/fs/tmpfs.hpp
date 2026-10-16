#pragma once

#include "filesystem.hpp"
#include "phys_region.hpp"

namespace astia::fs {

    inline constexpr u32   tmpfs_max_entries = 32;
    inline constexpr usize tmpfs_max_pages   = 16;

    class tmpfs_file final : public file {
        public:
        explicit tmpfs_file(usize pages = tmpfs_max_pages);
        ~tmpfs_file();

        long             read(void* buffer, u32 length, u64 offset) override;
        long             write(const void* buffer, u32 length, u64 offset) override;
        long             size() const override;
        mm::phys_region* memory_region() override {
            return storage_;
        }
        void truncate() {
            size_ = 0;
        }

        private:
        mm::phys_region* storage_ = nullptr;
        u64              size_    = 0;
    };

    class tmpfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;

        private:
        struct entry {
            bool                  used = false;
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
