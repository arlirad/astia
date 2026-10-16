#pragma once

#include <astia/fs/filesystem.hpp>

namespace astia::fs {

    inline constexpr u32 devfs_max_entries = 32;

    class devfs_mount final : public mount {
        public:
        auto open(const char* path, u32 flags) -> mm::shptr<open_file> override;
        auto register_file(const char* path, const mm::shptr<file>& object)
            -> long override;

        private:
        struct entry {
            bool            used;
            char            path[path_max];
            mm::shptr<file> object;
        };

        entry entries_[devfs_max_entries]{};
    };

    class devfs final : public filesystem {
        public:
        constexpr devfs() = default;

        auto name() const -> const char* override {
            return "devfs";
        }
        auto mount(const char* source) const -> fs::mount* override;

        auto attach(devfs_mount* instance) -> void {
            instance_ = instance;
        }

        private:
        devfs_mount* instance_ = nullptr;
    };

} // namespace astia::fs
