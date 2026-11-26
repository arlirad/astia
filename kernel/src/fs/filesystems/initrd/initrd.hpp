#pragma once

#include <astia/span.hpp>

#include "../../filesystem.hpp"

#include <initrd.hpp>

namespace astia::fs::filesystems {
    inline constexpr u32 initrd_max_entries = 128;

    class initrd_mount final : public mount {
        public:
        bool attach(const initrd::archive& archive);
        bool add(const char* path, astia::span<const u8> data);

        bool add(const char* path, const void* data, usize size) {
            return add(path, astia::span<const u8>(static_cast<const u8*>(data), size));
        }

        bool read_only() const override {
            return true;
        }

        mm::shptr<open_file> open(const char* path, u32 flags) override;
        long                 mkdir(const char* path, file_mode mode) override;
        long                 unlink(const char* path, bool directory) override;
        long                 rename(const char* old_path, const char* new_path) override;
        long                 link(const char* old_path, const char* new_path) override;
        long                 symlink(const char* target, const char* path) override;
        long      chmod(const char* path, file_permissions permissions) override;
        long      chown(const char* path, uid_t uid, gid_t gid) override;
        file_type node_type(const char* path) const override;
        file_mode node_mode(const char* path) const override;
        long      read_directory(const char* path, u64 index, char* name, u32 capacity,
                                 file_type* type) override;

        private:
        struct entry {
            bool                  used;
            char                  path[path_max];
            astia::span<const u8> data;
        };

        entry _entries[initrd_max_entries]{};
        // An attached initrd is immutable for the lifetime of the cached
        // instance.  In particular, attach() must never clear this object
        // while open_file objects can still retain its mount pointer.
        bool _attached = false;
    };

    class initrd_filesystem final : public filesystem {
        public:
        const char* name() const override {
            return "initrd";
        }

        fs::mount* mount(const char* source) const override;
        bool       attach(const initrd::archive& archive);

        private:
        mm::shptr<initrd_mount> _instance;
    };

    initrd_filesystem& initrd_type();
} // namespace astia::fs::filesystems
