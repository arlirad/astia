#pragma once

#include <astia/fs/filesystem.hpp>
#include <astia/initrd/initrd.hpp>

namespace astia::fs {

    inline constexpr u32 initrd_max_entries = 64;

    class initrd_mount final : public mount {
        public:
        auto attach(const initrd::archive& archive) -> bool;
        auto add(const char* path, const void* data, usize size) -> bool;
        auto open(const char* path, u32 flags) -> mm::shptr<open_file> override;

        private:
        struct entry {
            bool        used;
            char        path[path_max];
            const void* data;
            usize       size;
        };

        entry entries_[initrd_max_entries]{};
    };

    class initrd_filesystem final : public filesystem {
        public:
        auto name() const -> const char* override {
            return "initrd";
        }
        auto mount(const char* source) const -> fs::mount* override;
        auto attach(const initrd::archive& archive) -> bool;

        private:
        initrd_mount* instance_ = nullptr;
    };

    auto initrd_type() -> initrd_filesystem&;

} // namespace astia::fs
