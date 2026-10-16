#pragma once

#include "filesystem.hpp"
#include "phys_region.hpp"

namespace astia::fs {

    inline constexpr u32 tmpfs_max_entries = 32;
    inline constexpr usize tmpfs_max_pages = 16;

    class tmpfs_file final : public file {
        public:
        explicit tmpfs_file(usize pages = tmpfs_max_pages);
        ~tmpfs_file();

        auto read(void* buffer, u32 length, u64 offset) -> long override;
        auto write(const void* buffer, u32 length, u64 offset) -> long override;
        auto size() const -> long override;
        auto memory_region() -> mm::phys_region* override {
            return storage_;
        }
        auto truncate() -> void {
            size_ = 0;
        }

        private:
        mm::phys_region* storage_ = nullptr;
        u64               size_ = 0;
    };

    class tmpfs_mount final : public mount {
        public:
        auto open(const char* path, u32 flags) -> mm::shptr<open_file> override;

        private:
        struct entry {
            bool used = false;
            char path[path_max] {};
            mm::shptr<tmpfs_file> object;
        };
        entry entries_[tmpfs_max_entries] {};
    };

    class tmpfs final : public filesystem {
        public:
        auto name() const -> const char* override {
            return "tmpfs";
        }
        auto mount(const char* source) const -> fs::mount* override;

        private:
        mutable tmpfs_mount* instance_ = nullptr;
    };

} // namespace astia::fs
