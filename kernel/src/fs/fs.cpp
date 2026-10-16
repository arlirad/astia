#include "devfs.hpp"
#include "initrd_fs.hpp"
#include "tmpfs.hpp"
#include <astia/api/errno.h>
#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

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

        bool equal(const char* lhs, const char* rhs) {
            if (lhs == nullptr || rhs == nullptr)
                return false;
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        bool copy_path(char* dst, const char* src) {
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

        const filesystem* find_type(const char* name) {
            for (u32 i = 0; i < max_types; ++i) {
                if (types[i].type != nullptr && equal(types[i].type->name(), name)) {
                    return types[i].type;
                }
            }
            return nullptr;
        }

        bool path_matches(const char* target, const char* path) {
            if (equal(target, "/")) {
                return path != nullptr && path[0] == '/';
            }
            u32 i = 0;
            while (target[i] != '\0' && target[i] == path[i])
                ++i;
            return target[i] == '\0' && (path[i] == '\0' || path[i] == '/');
        }

        mount_entry* find_mount(const char* path) {
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

        const char* relative_path(const mount_entry& entry, const char* path) {
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

    mm::shptr<open_file> mount::open(const char* path, u32 flags) {
        (void) path;
        (void) flags;
        return {};
    }

    long mount::register_file(const char* path, const mm::shptr<file>& object) {
        (void) path;
        (void) object;
        return -ENOSYS;
    }

    bool init() {
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

    long register_type(const filesystem* type) {
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

    long mount_type(const char* type_name, const char* target, const char* source) {
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

    long register_device(const char* path, const mm::shptr<file>& object) {
        mount_entry* entry = find_mount(path);
        if (entry == nullptr)
            return -ENOENT;
        return entry->instance->register_file(relative_path(*entry, path), object);
    }

    mm::shptr<open_file> open_path(const char* path, u32 flags) {
        mount_entry* entry = find_mount(path);
        if (entry == nullptr)
            return {};
        return entry->instance->open(relative_path(*entry, path), flags);
    }

    mm::shptr<open_file> open(const char* path, u32 flags) {
        return open_path(path, flags);
    }

    // The remaining file/open-file implementation stays in this TU.
    long file::read(void* buf, u32 len, u64 offset) {
        (void) buf;
        (void) len;
        (void) offset;
        return -ENOSYS;
    }
    long file::write(const void* buf, u32 len, u64 offset) {
        (void) buf;
        (void) len;
        (void) offset;
        return -ENOSYS;
    }
    long file::submit(io_request& request) {
        // Synchronous default: a node that finishes inline answers with
        // whatever its read()/write() returns.  Note the trap — returning 0
        // here parks forever, so a node that can genuinely be "not ready"
        // must override submit() and set a stage.
        return request.direction() == io_direction::input
                   ? read(request.buffer(), request.length(), request.offset())
                   : write(request.buffer(), request.length(), request.offset());
    }
    long file::ioctl(u64 request, void* argp) {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

    long file::size() const {
        return -ESPIPE;
    }

    mm::phys_region* file::memory_region() {
        return nullptr;
    }
    long open_file::read(void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        const u64  actual = offset == offset_current ? _position : offset;
        const long result = _object->read(buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    long open_file::write(const void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        const u64  actual = offset == offset_current ? _position : offset;
        const long result = _object->write(buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    long open_file::submit(io_request& request) {
        if (!_object)
            return -EBADF;
        // Resolve the offset once, at submission, and write it back into
        // the request: every resume of a parked operation must land on the
        // same position the caller asked for, not on a moved _position.
        const u64 actual =
            request.offset() == offset_current ? _position : request.offset();
        request.offset(actual);
        const long result = _object->submit(request);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    long open_file::ioctl(u64 request, void* argp) {
        return !_object ? -EBADF : _object->ioctl(request, argp);
    }

    long open_file::size() const {
        return !_object ? -EBADF : _object->size();
    }

} // namespace astia::fs
