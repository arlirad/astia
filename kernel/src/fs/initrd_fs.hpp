#pragma once

#include "filesystem.hpp"
#include "initrd.hpp"

namespace astia::fs {

    inline constexpr u32 initrd_max_entries = 64;

    class initrd_mount final : public mount {
        public:
        bool                 attach(const initrd::archive& archive);
        bool                 add(const char* path, const void* data, usize size);
        mm::shptr<open_file> open(const char* path, u32 flags) override;
        u32 node_mode(const char* path) const override;
        long read_directory(const char* path, u64 index, char* name, u32 capacity,
                            u32* type) override;

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
