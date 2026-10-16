#include "initrd_fs.hpp"

#include "share.hpp"
#include <astia/api/errno.h>

namespace astia::fs {

    namespace {
        class initrd_file final : public file {
            public:
            initrd_file(const void* data, usize size) : data_(data), size_(size) {}

            auto read(void* buffer, u32 length, u64 offset) -> long override {
                if (offset >= size_)
                    return 0;
                const usize available = size_ - static_cast<usize>(offset);
                const usize count     = available < length ? available : length;
                const auto* source    = static_cast<const u8*>(data_) + offset;
                auto*       target    = static_cast<u8*>(buffer);
                for (usize i = 0; i < count; ++i)
                    target[i] = source[i];
                return static_cast<long>(count);
            }

            auto write(const void*, u32, u64) -> long override {
                return -EROFS;
            }
            auto size() const -> long override {
                return static_cast<long>(size_);
            }

            private:
            const void* data_;
            usize       size_;
        };

        auto equal(const char* lhs, const char* rhs) -> bool {
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        auto copy_path(char* dst, const char* src) -> bool {
            if (src == nullptr)
                return false;
            u32 i = 0;
            for (; src[i] != '\0' && i + 1 < path_max; ++i)
                dst[i] = src[i];
            if (src[i] != '\0')
                return false;
            dst[i] = '\0';
            return true;
        }

        auto add_file(void* opaque, const initrd::file& source) -> bool {
            auto& mount = *static_cast<initrd_mount*>(opaque);
            return mount.add(source.name, source.data, source.size);
        }
    }

    auto initrd_mount::add(const char* path, const void* data, usize size) -> bool {
        if (path == nullptr || data == nullptr || size == 0)
            return false;
        char normalized[path_max]{};
        normalized[0] = '/';
        if (!copy_path(normalized + 1, path))
            return false;
        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, normalized))
                return false;
        }
        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (!entries_[i].used) {
                entries_[i].used = true;
                copy_path(entries_[i].path, normalized);
                entries_[i].data = data;
                entries_[i].size = size;
                return true;
            }
        }
        return false;
    }

    auto initrd_mount::attach(const initrd::archive& archive) -> bool {
        *this = initrd_mount{};
        return initrd::each(archive, add_file, this);
    }

    auto initrd_mount::open(const char* path, u32 flags) -> mm::shptr<open_file> {
        (void) flags;
        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (entries_[i].used && equal(entries_[i].path, path)) {
                mm::shptr<initrd_file> object = mm::shptr<initrd_file>(
                    new initrd_file(entries_[i].data, entries_[i].size));
                return object ? mm::share<open_file>(object, flags)
                              : mm::shptr<open_file>{};
            }
        }
        return {};
    }

    auto initrd_filesystem::attach(const initrd::archive& archive) -> bool {
        if (instance_ == nullptr) {
            instance_ = new initrd_mount{};
        }
        return instance_ != nullptr && instance_->attach(archive);
    }

    auto initrd_filesystem::mount(const char*) const -> fs::mount* {
        return instance_;
    }

    auto initrd_type() -> initrd_filesystem& {
        static initrd_filesystem type;
        return type;
    }

} // namespace astia::fs
