#pragma once

#include "filesystem.hpp"
#include <astia/core/spinlock.hpp>

namespace astia::fs {

    inline constexpr u32 devfs_max_entries = 32;

    class devfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        file_type            node_type(const char* path) const override;
        file_mode            node_mode(const char* path) const override;
        uid_t                node_uid(const char* path) const override;
        gid_t                node_gid(const char* path) const override;
        long register_file(const char* path, const mm::shptr<file>& object) override;
        long unregister_file(const char* path, const mm::shptr<file>& object) override;

        private:
        struct entry {
            bool            used;
            char            path[path_max];
            mm::shptr<file> object;
        };

        entry entries_[devfs_max_entries]{};
        // Protects entry publication and lookup; object methods run after
        // lookup with a retained reference and without this lock held.
        mutable core::spinlock namespace_lock_;
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
