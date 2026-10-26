#pragma once

#include "filesystem.hpp"

namespace astia::fs {

    inline constexpr u32 devfs_max_entries = 32;

    class devfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        u32 node_type(const char* path) const override;
        u32 node_mode(const char* path) const override;
        u32 node_uid(const char* path) const override;
        u32 node_gid(const char* path) const override;
        long register_file(const char* path, const mm::shptr<file>& object) override;

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

        const char* name() const override {
            return "devfs";
        }
        fs::mount* mount(const char* source) const override;

        void attach(devfs_mount* instance) {
            instance_ = instance;
        }

        private:
        devfs_mount* instance_ = nullptr;
    };

} // namespace astia::fs
