#pragma once

#include <astia/fs/filesystem.hpp>
#include <astia/fs/mount.hpp>

namespace astia::fs::filesystems {
    /// Mount implementation for the device namespace.  Node state itself is
    /// owned by the generic core registry; this class owns /dev policy:
    /// special names, permissions, pty synthesis, and metadata exposure.
    class devfs_mount final : public mount {
        public:
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        file_type            node_type(const char* path) const override;
        file_mode            node_mode(const char* path) const override;
        uid_t                node_uid(const char* path) const override;
        gid_t                node_gid(const char* path) const override;
    };

    class devfs final : public filesystem {
        public:
        constexpr devfs() = default;

        const char* name() const override {
            return "devfs";
        }

        fs::mount* mount(const char* source) const override;

        void attach(devfs_mount* instance) {
            _instance = instance;
        }

        private:
        devfs_mount* _instance = nullptr;
    };
} // namespace astia::fs::filesystems
