#include "initrd.hpp"

#include <astia/api/errno.h>
#include <astia/mm/share.hpp>
#include <astia/strings.hpp>

namespace astia::fs::filesystems {
    namespace {
        class initrd_file final : public file, public readable, public writable {
            public:
            explicit initrd_file(astia::span<const u8> data) : _data(data) {}

            optional<readable&> as_readable() override {
                return *this;
            }

            optional<writable&> as_writable() override {
                return *this;
            }

            core::task<i64> readv(span<const buffer> buffers, u64 offset,
                                  core::cltoken token) override {
                return submit_readv(buffers, offset, token);
            }

            core::task<i64> writev(span<const buffer> buffers, u64 offset,
                                   core::cltoken token, bool nonblock) override {
                return submit_writev(buffers, offset, token, nonblock);
            }

            long read_ready(void* buffer, u32 length, u64 offset) override {
                if (buffer == nullptr && length != 0)
                    return -EFAULT;
                if (offset >= _data.size())
                    return 0;
                const usize available = _data.size() - static_cast<usize>(offset);
                const usize count     = available < length ? available : length;
                const auto* source    = _data.data() + offset;
                auto*       target    = static_cast<u8*>(buffer);
                for (usize i = 0; i < count; ++i)
                    target[i] = source[i];
                return static_cast<long>(count);
            }

            long write_ready(const void*, u32, u64) override {
                return -EROFS;
            }

            long size() const override {
                return static_cast<long>(_data.size());
            }

            file_mode mode() const override {
                return file_mode(file_type::regular, readonly_file_permissions);
            }

            private:
            astia::span<const u8> _data;
        };

        bool copy_path(char* dst, const char* src) {
            if (src == nullptr)
                return false;

            u32 i = 0;

            for (; src[i] != '\0' && i + 1 < path_max; ++i) {
                dst[i] = src[i];
            }

            if (src[i] != '\0')
                return false;

            dst[i] = '\0';

            return true;
        }

        struct child_name_view {
            const char* name   = nullptr;
            u32         length = 0;
            file_type   type   = directory_unknown;
        };

        bool child_name(const char* directory, const char* full, child_name_view& child) {
            u32 directory_len = 0;
            u32 full_len      = 0;

            while (directory[directory_len] != '\0') {
                ++directory_len;
            }

            while (full[full_len] != '\0') {
                ++full_len;
            }

            const u32 start = directory_len == 1 ? 1 : directory_len + 1;
            if (start >= full_len)
                return false;

            if (directory_len != 1) {
                for (u32 i = 0; i < directory_len; ++i) {
                    if (full[i] != directory[i])
                        return false;
                }

                if (full[directory_len] != '/')
                    return false;
            }

            u32 end = start;

            while (end < full_len && full[end] != '/') {
                ++end;
            }

            child.name   = full + start;
            child.length = end - start;
            child.type   = end < full_len ? directory_dir : directory_reg;

            return true;
        }

        bool equal_name(const child_name_view& lhs, const child_name_view& rhs) {
            if (lhs.length != rhs.length)
                return false;

            for (u32 i = 0; i < lhs.length; ++i) {
                if (lhs.name[i] != rhs.name[i])
                    return false;
            }

            return true;
        }

        bool add_file(void* opaque, const initrd::file& source) {
            auto& mount = *static_cast<initrd_mount*>(opaque);

            return mount.add(
                source.name,
                astia::span<const u8>(static_cast<const u8*>(source.data), source.size));
        }
    }

    bool initrd_mount::add(const char* path, astia::span<const u8> data) {
        if (_attached || path == nullptr || data.data() == nullptr || data.empty())
            return false;

        char normalized[path_max]{};

        normalized[0] = '/';

        if (!copy_path(normalized + 1, path))
            return false;

        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, normalized))
                return false;
        }

        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (!_entries[i].used) {
                _entries[i].used = true;

                copy_path(_entries[i].path, normalized);

                _entries[i].data = data;

                return true;
            }
        }

        return false;
    }

    bool initrd_mount::attach(const initrd::archive& archive) {
        if (_attached)
            return false;

        if (!initrd::each(archive, add_file, this)) {
            // A failed parse never publishes this namespace.  Clear the
            // candidate so a caller may retry it, while attached instances
            // remain protected by the guard above.
            for (u32 i = 0; i < initrd_max_entries; ++i) {
                _entries[i] = entry{};
            }

            return false;
        }

        _attached = true;

        return true;
    }

    mm::shptr<open_file> initrd_mount::open(const char* path, u32 flags) {
        (void) flags;

        if (path == nullptr)
            return {};

        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path)) {
                auto file = mm::share<initrd_file>(_entries[i].data);

                return file ? mm::share<open_file>(file, flags) : mm::shptr<open_file>{};
            }
        }

        return {};
    }

    long initrd_mount::mkdir(const char*, file_mode) {
        return -EROFS;
    }

    long initrd_mount::unlink(const char*, bool) {
        return -EROFS;
    }

    long initrd_mount::rename(const char*, const char*) {
        return -EROFS;
    }

    long initrd_mount::link(const char*, const char*) {
        return -EROFS;
    }

    long initrd_mount::symlink(const char*, const char*) {
        return -EROFS;
    }

    long initrd_mount::chmod(const char*, file_permissions) {
        return -EROFS;
    }

    long initrd_mount::chown(const char*, uid_t, gid_t) {
        return -EROFS;
    }

    file_type initrd_mount::node_type(const char* path) const {
        if (path == nullptr || path[0] != '/')
            return directory_unknown;

        if (path[1] == '\0')
            return directory_dir;

        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (_entries[i].used && string_equal(_entries[i].path, path))
                return directory_reg;
        }

        // Directories are implicit in the archive namespace.
        u32 length = 0;

        while (path[length] != '\0') {
            ++length;
        }

        for (u32 i = 0; i < initrd_max_entries; ++i) {
            if (!_entries[i].used)
                continue;

            bool prefix = true;

            for (u32 j = 0; j < length; ++j) {
                if (_entries[i].path[j] != path[j])
                    prefix = false;
            }

            if (prefix && _entries[i].path[length] == '/')
                return directory_dir;
        }

        return directory_unknown;
    }

    file_mode initrd_mount::node_mode(const char* path) const {
        if (path != nullptr && path[0] == '/') {
            if (path[1] == '\0')
                return file_mode(file_type::directory, readonly_directory_permissions);

            for (u32 i = 0; i < initrd_max_entries; ++i) {
                if (_entries[i].used && string_equal(_entries[i].path, path))
                    return file_mode(file_type::regular, readonly_file_permissions);
            }

            /* Directories are implicit in the archive namespace. */
            for (u32 i = 0; i < initrd_max_entries; ++i) {
                if (_entries[i].used) {
                    u32 n = 0;

                    while (path[n] != '\0') {
                        ++n;
                    }

                    bool prefix = n == 1;
                    if (!prefix) {
                        prefix = true;

                        for (u32 j = 0; j < n; ++j) {
                            if (_entries[i].path[j] != path[j])
                                prefix = false;
                        }
                    }

                    if (prefix && _entries[i].path[n] == '/')
                        return file_mode(file_type::directory,
                                         readonly_directory_permissions);
                }
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
            if (!_entries[i].used)
                continue;

            child_name_view candidate;

            if (!child_name(path, _entries[i].path, candidate))
                continue;

            bool duplicate = false;

            for (u32 p = 0; p < i && !duplicate; ++p) {
                if (!_entries[p].used)
                    continue;

                child_name_view prior;

                if (child_name(path, _entries[p].path, prior) &&
                    equal_name(candidate, prior))
                    duplicate = true;
            }

            if (duplicate || ordinal++ != index)
                continue;

            if (candidate.length + 1 > capacity)
                return -ENAMETOOLONG;

            for (u32 j = 0; j < candidate.length; ++j) {
                name[j] = candidate.name[j];
            }

            name[candidate.length] = '\0';
            *type                  = candidate.type;

            return 1;
        }

        return 0;
    }

    bool initrd_filesystem::attach(const initrd::archive& archive) {
        // Publish only a completely parsed archive.  Once published, the
        // instance is immutable: replacing it would leave open_file mount
        // pointers referring to a reset or freed namespace.
        if (_instance)
            return false;

        auto candidate = mm::share<initrd_mount>();

        if (!candidate || !candidate->attach(archive))
            return false;

        _instance = candidate;

        return true;
    }

    fs::mount* initrd_filesystem::mount(const char*) const {
        return _instance.get();
    }

    initrd_filesystem& initrd_type() {
        static initrd_filesystem type;

        return type;
    }
} // namespace astia::fs::filesystems
