#pragma once

#include "filesystem.hpp"
#include "initrd.hpp"
#include <astia/span.hpp>

namespace astia::fs {

    inline constexpr u32 initrd_max_entries = 64;

    class initrd_mount final : public mount {
        public:
        bool attach(const initrd::archive& archive);
        bool add(const char* path, astia::span<const u8> data);
        bool add(const char* path, const void* data, usize size) {
            return add(path, astia::span<const u8>(static_cast<const u8*>(data), size));
        }
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        file_type            node_type(const char* path) const override;
        file_mode            node_mode(const char* path) const override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            file_type* type) override;

        private:
        struct entry {
            bool                  used;
            char                  path[path_max];
            astia::span<const u8> data;
        };

        entry entries_[initrd_max_entries]{};
        // An attached initrd is immutable for the lifetime of the cached
        // instance.  In particular, attach() must never clear this object
        // while open_file objects can still retain its mount pointer.
        bool attached_ = false;
    };

    class initrd_filesystem final : public filesystem {
        public:
        const char* name() const override {
            return "initrd";
        }
        fs::mount* mount(const char* source) const override;
        bool       attach(const initrd::archive& archive);

        private:
        initrd_mount* instance_ = nullptr;
    };

    initrd_filesystem& initrd_type();

} // namespace astia::fs
