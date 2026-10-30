#include "initrd_fs.hpp"

#include <astia/api/errno.h>
#include <astia/mm/share.hpp>

namespace astia::fs {

    namespace {
        class initrd_file final : public file {
            public:
            initrd_file(const void* data, usize size) : data_(data), size_(size) {}

            long read(void* buffer, u32 length, u64 offset) override {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;
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

            long write(const void*, u32, u64) override {
                return -EROFS;
            }
            long size() const override {
                return static_cast<long>(size_);
            }
            file_mode mode() const override { return file_mode(file_type::regular, file_permissions(0755)); }
            const void* capability(file_capability kind) const override;

            private:
            const void* data_;
            usize       size_;
        };

        long initrd_read(file* object, void* buffer, u32 length, u64 offset) {
            return static_cast<initrd_file*>(object)->read(buffer, length, offset);
        }
        long initrd_size(const file* object) {
            return static_cast<const initrd_file*>(object)->size();
        }

        const file_read_capability initrd_read_capability{initrd_read};
        const file_size_capability initrd_size_capability{initrd_size};

        const void* initrd_file::capability(file_capability kind) const {
            switch (kind) {
                case file_capability::read:  return &initrd_read_capability;
                case file_capability::write: return nullptr;
                case file_capability::size:  return &initrd_size_capability;
                case file_capability::mapping: return nullptr;
                default: return file::capability(kind);
            }
        }

        bool equal(const char* lhs, const char* rhs) {
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        bool copy_path(char* dst, const char* src) {
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

        bool child_name(const char* directory, const char* full, char* name, u32 capacity,
                         file_type& type) {
            u32 directory_len = 0;
            u32 full_len      = 0;
            while (directory[directory_len] != '\0')
                ++directory_len;
            while (full[full_len] != '\0')
                ++full_len;
            const u32 start = directory_len == 1 ? 1 : directory_len + 1;
            if (start >= full_len)
                return false;
            if (directory_len != 1) {
                for (u32 i = 0; i < directory_len; ++i)
                    if (full[i] != directory[i])
                        return false;
                if (full[directory_len] != '/')
                    return false;
            }
            u32 end = start;
            while (end < full_len && full[end] != '/')
                ++end;
            const u32 length = end - start;
            if (length + 1 > capacity)
                return false;
            for (u32 i = 0; i < length; ++i)
                name[i] = full[start + i];
            name[length] = '\0';
            type         = end < full_len ? directory_dir : directory_reg;
            return true;
        }

        bool add_file(void* opaque, const initrd::file& source) {
            auto& mount = *static_cast<initrd_mount*>(opaque);
            return mount.add(source.name, source.data, source.size);
        }
    }

    bool initrd_mount::add(const char* path, const void* data, usize size) {
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

    bool initrd_mount::attach(const initrd::archive& archive) {
        *this = initrd_mount{};
        return initrd::each(archive, add_file, this);
    }

    mm::shptr<open_file> initrd_mount::open(const char* path, u32 flags) {
        (void) flags;
        if (path == nullptr)
            return {};
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

    file_type initrd_mount::node_type(const char* path) const {
        if (path == nullptr || path[0] != '/') return directory_unknown;
        if (path[1] == '\0') return directory_dir;
        for (u32 i = 0; i < initrd_max_entries; ++i)
            if (entries_[i].used && equal(entries_[i].path, path)) return directory_reg;
        // Directories are implicit in the archive namespace.
        u32 length = 0;
        while (path[length] != '\0') ++length;
        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (!entries_[i].used) continue;
            bool prefix = true;
            for (u32 j = 0; j < length; ++j)
                if (entries_[i].path[j] != path[j]) prefix = false;
            if (prefix && entries_[i].path[length] == '/') return directory_dir;
        }
        return directory_unknown;
    }

    file_mode initrd_mount::node_mode(const char* path) const {
        if (path != nullptr && path[0] == '/') {
            if (path[1] == '\0') return file_mode(file_type::directory, file_permissions(0755));
            for (u32 i = 0; i < initrd_max_entries; ++i)
                if (entries_[i].used && equal(entries_[i].path, path)) return file_mode(file_type::regular, file_permissions(0755));
            /* Directories are implicit in the archive namespace. */
            for (u32 i = 0; i < initrd_max_entries; ++i)
                if (entries_[i].used) {
                    u32 n = 0;
                    while (path[n] != '\0') ++n;
                    bool prefix = n == 1;
                    if (!prefix) {
                        prefix = true;
                        for (u32 j = 0; j < n; ++j)
                            if (entries_[i].path[j] != path[j]) prefix = false;
                    }
                    if (prefix && entries_[i].path[n] == '/') return file_mode(file_type::directory, file_permissions(0755));
                }
        }
        return {};
    }

    long initrd_mount::read_directory(const char* path, u64 index, char* name,
                                       u32 capacity, file_type* type) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;
        u64 ordinal = 0;
        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (!entries_[i].used)
                continue;
            const char* full = entries_[i].path;
            char        candidate[256]{};
            file_type   candidate_type = directory_unknown;
            if (!child_name(path, full, candidate, sizeof candidate, candidate_type))
                continue;
            bool duplicate = false;
            for (u32 p = 0; p < i && !duplicate; ++p) {
                if (!entries_[p].used)
                    continue;
                char prior[256]{};
                file_type prior_type = directory_unknown;
                if (child_name(path, entries_[p].path, prior, sizeof prior, prior_type) &&
                    equal(candidate, prior))
                    duplicate = true;
            }
            if (duplicate || ordinal++ != index)
                continue;
            u32 length = 0;
            while (candidate[length] != '\0')
                ++length;
            for (u32 j = 0; j <= length; ++j)
                name[j] = candidate[j];
            *type = candidate_type;
            return 1;
        }
        return 0;
    }

    bool initrd_filesystem::attach(const initrd::archive& archive) {
        if (instance_ == nullptr) {
            instance_ = new initrd_mount{};
        }
        return instance_ != nullptr && instance_->attach(archive);
    }

    fs::mount* initrd_filesystem::mount(const char*) const {
        return instance_;
    }

    initrd_filesystem& initrd_type() {
        static initrd_filesystem type;
        return type;
    }

} // namespace astia::fs
