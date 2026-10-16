#include <astia/api/errno.h>
#include "devfs.hpp"
#include "fs.hpp"
#include "initrd_fs.hpp"
#include "tmpfs.hpp"
#include "share.hpp"

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::fs {

    namespace {
        inline constexpr u32 max_types  = 16;
        inline constexpr u32 max_mounts = 16;

        struct type_entry {
            const filesystem* type;
        };

        struct mount_entry {
            bool   used;
            char   target[path_max];
            mount* instance;
        };

        type_entry  types[max_types];
        mount_entry mounts[max_mounts];
        bool        ready;

        auto equal(const char* lhs, const char* rhs) -> bool {
            if (lhs == nullptr || rhs == nullptr)
                return false;
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        auto copy_path(char* dst, const char* src) -> bool {
            if (src == nullptr || src[0] != '/')
                return false;
            u32 i = 0;
            for (; src[i] != '\0' && i + 1 < path_max; ++i)
                dst[i] = src[i];
            if (src[i] != '\0')
                return false;
            dst[i] = '\0';
            return true;
        }

        auto find_type(const char* name) -> const filesystem* {
            for (u32 i = 0; i < max_types; ++i) {
                if (types[i].type != nullptr && equal(types[i].type->name(), name)) {
                    return types[i].type;
                }
            }
            return nullptr;
        }

        auto path_matches(const char* target, const char* path) -> bool {
            if (equal(target, "/")) {
                return path != nullptr && path[0] == '/';
            }
            u32 i = 0;
            while (target[i] != '\0' && target[i] == path[i])
                ++i;
            return target[i] == '\0' && (path[i] == '\0' || path[i] == '/');
        }

        auto find_mount(const char* path) -> mount_entry* {
            mount_entry* best     = nullptr;
            u32          best_len = 0;
            for (u32 i = 0; i < max_mounts; ++i) {
                if (!mounts[i].used || !path_matches(mounts[i].target, path))
                    continue;
                u32 length = 0;
                while (mounts[i].target[length] != '\0')
                    ++length;
                if (best == nullptr || length > best_len) {
                    best     = &mounts[i];
                    best_len = length;
                }
            }
            return best;
        }

        auto relative_path(const mount_entry& entry, const char* path) -> const char* {
            if (equal(entry.target, "/")) {
                return path;
            }
            const char* relative = path;
            while (*relative != '\0' && *relative == entry.target[relative - path]) {
                ++relative;
            }
            return *relative == '\0' ? "/" : relative;
        }

        devfs devfs_type;
        tmpfs tmpfs_type_instance;
        alignas(devfs_mount) u8 devfs_storage[sizeof(devfs_mount)];
        devfs_mount* devfs_instance;
    } // namespace

    auto mount::open(const char* path, u32 flags) -> mm::shptr<open_file> {
        (void) path;
        (void) flags;
        return {};
    }

    auto mount::register_file(const char* path, const mm::shptr<file>& object) -> long {
        (void) path;
        (void) object;
        return -ENOSYS;
    }

    auto init() -> bool {
        for (u32 i = 0; i < max_types; ++i)
            types[i] = type_entry{};
        for (u32 i = 0; i < max_mounts; ++i)
            mounts[i] = mount_entry{};
        ready = true;

        devfs_instance = ::new (devfs_storage) devfs_mount{};
        devfs_type.attach(devfs_instance);
        return register_type(&devfs_type) == 0 && register_type(&initrd_type()) == 0 &&
               register_type(&tmpfs_type_instance) == 0 &&
               mount_type("devfs", "/dev") == 0 && mount_type("tmpfs", "/tmp") == 0;
    }

    auto register_type(const filesystem* type) -> long {
        if (!ready || type == nullptr || type->name() == nullptr)
            return -EINVAL;
        if (find_type(type->name()) != nullptr)
            return -EEXIST;
        for (u32 i = 0; i < max_types; ++i) {
            if (types[i].type == nullptr) {
                types[i].type = type;
                return 0;
            }
        }
        return -ENFILE;
    }

    auto mount_type(const char* type_name, const char* target, const char* source)
        -> long {
        if (!ready || target == nullptr || target[0] != '/')
            return -EINVAL;
        const filesystem* type = find_type(type_name);
        if (type == nullptr)
            return -ENODEV;
        for (u32 i = 0; i < max_mounts; ++i) {
            if (mounts[i].used && equal(mounts[i].target, target))
                return -EBUSY;
        }
        mount* instance = type->mount(source);
        if (instance == nullptr)
            return -ENOMEM;
        for (u32 i = 0; i < max_mounts; ++i) {
            if (!mounts[i].used) {
                if (!copy_path(mounts[i].target, target))
                    return -ENAMETOOLONG;
                mounts[i].used     = true;
                mounts[i].instance = instance;
                return 0;
            }
        }
        return -ENFILE;
    }

    auto register_device(const char* path, const mm::shptr<file>& object) -> long {
        mount_entry* entry = find_mount(path);
        if (entry == nullptr)
            return -ENOENT;
        return entry->instance->register_file(relative_path(*entry, path), object);
    }

    auto open_path(const char* path, u32 flags) -> mm::shptr<open_file> {
        mount_entry* entry = find_mount(path);
        if (entry == nullptr)
            return {};
        return entry->instance->open(relative_path(*entry, path), flags);
    }

    auto open(const char* path, u32 flags) -> mm::shptr<open_file> {
        return open_path(path, flags);
    }

    // The remaining file/open-file implementation stays in this TU.
    auto file::read(void* buf, u32 len, u64 offset) -> long {
        (void) buf;
        (void) len;
        (void) offset;
        return -ENOSYS;
    }
    auto file::write(const void* buf, u32 len, u64 offset) -> long {
        (void) buf;
        (void) len;
        (void) offset;
        return -ENOSYS;
    }
    auto file::ioctl(u64 request, void* argp) -> long {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

    auto file::size() const -> long {
        return -ESPIPE;
    }

    auto file::memory_region() -> mm::phys_region* {
        return nullptr;
    }
    auto open_file::read(void* buf, u32 len, u64 offset) -> long {
        if (!_object)
            return -EBADF;
        const u64  actual = offset == offset_current ? _position : offset;
        const long result = _object->read(buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    auto open_file::write(const void* buf, u32 len, u64 offset) -> long {
        if (!_object)
            return -EBADF;
        const u64  actual = offset == offset_current ? _position : offset;
        const long result = _object->write(buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    auto open_file::ioctl(u64 request, void* argp) -> long {
        return !_object ? -EBADF : _object->ioctl(request, argp);
    }

    auto open_file::size() const -> long {
        return !_object ? -EBADF : _object->size();
    }

} // namespace astia::fs
