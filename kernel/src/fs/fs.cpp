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

        u32 length_of(const char* value) {
            u32 n = 0; while (value != nullptr && value[n] != '\0') ++n; return n;
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

    long mount::read_directory(const char*, u64, char*, u32, file_type*) {
        return -ENOSYS;
    }
    long mount::mkdir(const char*, file_mode) { return -ENOSYS; }
    long mount::unlink(const char*, bool) { return -ENOSYS; }
    long mount::rename(const char*, const char*) { return -ENOSYS; }
    file_type mount::node_type(const char*) const { return directory_unknown; }
    file_mode mount::node_mode(const char*) const { return {}; }
    uid_t mount::node_uid(const char*) const { return {}; }
    gid_t mount::node_gid(const char*) const { return {}; }
    uid_t file::uid() const { return {}; }
    gid_t file::gid() const { return {}; }
    long mount::chmod(const char*, file_permissions) { return -ENOSYS; }
    long mount::chown(const char*, uid_t, gid_t) { return -ENOSYS; }
    long mount::symlink(const char*, const char*) { return -ENOSYS; }
    long mount::readlink(const char*, char*, u32) const { return -ENOSYS; }

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
        char resolved[path_max]{};
        if (resolve_path(path, resolved, sizeof resolved, true) < 0)
            return {};
        mount_entry* entry = find_mount(resolved);
        if (entry == nullptr)
            return {};
        return entry->instance->open(relative_path(*entry, resolved), flags);
    }

    mm::shptr<open_file> open(const char* path, u32 flags) {
        return open_path(path, flags);
    }

    long read_directory(const char* path, u64 index, char* name, u32 capacity,
                         file_type* type) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr)
            return -ENOENT;
        return entry->instance->read_directory(relative_path(*entry, path), index, name,
                                               capacity, type);
    }

    long make_directory(const char* path, file_mode mode) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr) return -ENOENT;
        return entry->instance->mkdir(relative_path(*entry, path), mode);
    }

    long remove_path(const char* path, bool directory) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr) return -ENOENT;
        return entry->instance->unlink(relative_path(*entry, path), directory);
    }

    long rename_path(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        mount_entry* old_entry = find_mount(old_path);
        mount_entry* new_entry = find_mount(new_path);
        if (old_entry == nullptr || new_entry == nullptr)
            return -ENOENT;
        if (old_entry->instance != new_entry->instance)
            return -EXDEV;
        return old_entry->instance->rename(relative_path(*old_entry, old_path),
                                            relative_path(*new_entry, new_path));
    }

    file_type node_type(const char* path) {
        if (path == nullptr || path[0] != '/') return directory_unknown;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr) return directory_unknown;
        return entry->instance->node_type(relative_path(*entry, path));
    }

    long resolve_path(const char* path, char* buffer, u32 capacity, bool follow_final) {
        if (path == nullptr || buffer == nullptr || capacity == 0 || path[0] != '/')
            return -EINVAL;
        char pending[path_max * 2]{};
        char result[path_max]{};
        u32 plen = 0, rlen = 1, links = 0;
        result[0] = '/';
        for (u32 i = 0; path[i] != '\0'; ++i) {
            if (plen + 1 >= sizeof pending) return -ENAMETOOLONG;
            pending[plen++] = path[i];
        }
        pending[plen] = '\0';
        for (;;) {
            u32 start = 0;
            while (start < plen && pending[start] == '/') ++start;
            if (start == plen) break;
            u32 end = start;
            while (end < plen && pending[end] != '/') ++end;
            char component[path_max]{};
            u32 clen = end - start;
            if (clen >= path_max) return -ENAMETOOLONG;
            for (u32 i = 0; i < clen; ++i) component[i] = pending[start + i];
            component[clen] = '\0';
            for (u32 i = end; i <= plen; ++i) pending[i - end] = pending[i];
            plen -= end;
            while (plen != 0 && pending[0] == '/') { for (u32 i=0;i<plen;++i) pending[i]=pending[i+1]; --plen; }
            if (equal(component, ".")) continue;
            if (equal(component, "..")) {
                if (rlen > 1) { --rlen; while (rlen > 1 && result[rlen-1] != '/') --rlen; }
                continue;
            }
            if (rlen > 1) result[rlen++] = '/';
            if (rlen + clen >= sizeof result) return -ENAMETOOLONG;
            for (u32 i=0;i<clen;++i) result[rlen++]=component[i];
            result[rlen]='\0';
            const bool last = plen == 0;
            if ((!last || follow_final) && node_type(result) == directory_lnk) {
                if (++links > 16) return -ELOOP;
                char target[path_max]{};
                long n = read_symlink(result, target, sizeof target);
                if (n < 0) return n;
                char combined[path_max * 2]{};
                u32 at = 0;
                if (target[0] != '/') {
                    at = rlen;
                    while (at > 1 && result[at-1] != '/') --at;
                    for (u32 i=0;i<at;++i) combined[i]=result[i];
                }
                for (u32 i=0; target[i] != '\0'; ++i) {
                    if (at + i + 1 >= sizeof combined) return -ENAMETOOLONG;
                    combined[at+i]=target[i];
                }
                u32 tail = 0;
                while (pending[tail] != '\0') ++tail;
                const u32 target_len = length_of(target);
                if (tail != 0) { combined[at + target_len]='/'; for (u32 i=0;i<=tail;++i) combined[at+target_len+1+i]=pending[i]; }
                plen = at + target_len + (tail ? tail + 1 : 0);
                for (u32 i=0;i<=plen;++i) pending[i]=combined[i];
                rlen = 1; result[0]='/';
            }
        }
        // rlen excludes the terminating NUL; leave room for it in the
        // caller's buffer before copying the complete string below.
        if (rlen + 1 > capacity) return -ENAMETOOLONG;
        for (u32 i=0;i<=rlen;++i) buffer[i]=result[i];
        return 0;
    }

    long make_symlink(const char* target, const char* path) {
        mount_entry* e=find_mount(path); if (!e) return -ENOENT;
        return e->instance->symlink(target, relative_path(*e,path));
    }
    long read_symlink(const char* path, char* buffer, u32 capacity) {
        mount_entry* e=find_mount(path); if (!e) return -ENOENT;
        return e->instance->readlink(relative_path(*e,path), buffer, capacity);
    }

    file_mode node_mode(const char* path) {
        if (path == nullptr || path[0] != '/') return {};
        mount_entry* entry = find_mount(path);
        return entry == nullptr ? file_mode{} : entry->instance->node_mode(relative_path(*entry, path));
    }
    uid_t node_uid(const char* path) { mount_entry* e=find_mount(path); return e ? e->instance->node_uid(relative_path(*e,path)) : uid_t{}; }
    gid_t node_gid(const char* path) { mount_entry* e=find_mount(path); return e ? e->instance->node_gid(relative_path(*e,path)) : gid_t{}; }

    long chmod_path(const char* path, file_permissions permissions) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr) return -ENOENT;
        return entry->instance->chmod(relative_path(*entry, path), permissions);
    }

    long chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, follow_final);
        if (rc < 0) return rc;
        mount_entry* entry = find_mount(resolved);
        return entry == nullptr ? -ENOENT : entry->instance->chown(relative_path(*entry, resolved), uid, gid);
    }

    long touch_path(const char* path, u64 sec, u64 nsec) {
        if (path == nullptr || path[0] != '/') return -EINVAL;
        mount_entry* entry = find_mount(path);
        if (entry == nullptr) return -ENOENT;
        auto object = entry->instance->open(relative_path(*entry, path), 0);
        if (!object) return -ENOENT;
        return object->backing()->touch(sec, nsec);
    }

    bool read_batch(kernel_read* requests, usize count) {
        if (requests == nullptr)
            return false;
        for (usize i = 0; i < count; ++i) {
            if (requests[i].file == nullptr || requests[i].buffer == nullptr)
                return false;
            requests[i].result = requests[i].file->read(
                requests[i].buffer, requests[i].length, requests[i].offset);
            if (requests[i].result < 0)
                return false;
        }
        return true;
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
        // whatever its read()/write() returns.  A 0 from it here is not the
        // device contract's "park me" — a file has no device to resume it,
        // and end of file is exactly a completion with nothing moved — so it
        // is marked complete before the engine can mistake the two and wait
        // for a resume that never comes.  A node that genuinely can be "not
        // ready" overrides submit(), parks, and resumes by stage instead.
        const long result =
            request.direction() == io_direction::input
                ? read(request.buffer(), request.length(), request.offset())
                : write(request.buffer(), request.length(), request.offset());
        if (result == 0) {
            request.complete(true);
        }
        return result;
    }
    long file::ioctl(u64 request, void* argp) {
        (void) request;
        (void) argp;
        return -ENOTTY;
    }

    long file::size() const {
        return -ESPIPE;
    }

    file_mode file::mode() const { return file_mode(file_type::regular, file_permissions(0666)); }
    u64 file::mtime_sec() const { return 0; }
    u64 file::mtime_nsec() const { return 0; }
    long file::touch(u64, u64) { return -EROFS; }

    mm::phys_region* file::memory_region() {
        return nullptr;
    }
    void file::close(u32) {}

    open_file::~open_file() {
        if (_object)
            _object->close(_flags);
    }

    long open_file::read(void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        const u64 actual = offset == offset_current ? _position : offset;
        const long result = _object->read(buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    long open_file::write(const void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        u64 actual = offset == offset_current ? _position : offset;
        if ((_flags & 0x0400u) != 0) {
            const long end = _object->size();
            if (end < 0)
                return end;
            actual = static_cast<u64>(end);
        }
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
        u64 actual = request.offset() == offset_current ? _position : request.offset();
        if ((_flags & 0x0400u) != 0 && request.direction() == io_direction::output) {
            const long end = _object->size();
            if (end < 0)
                return end;
            actual = static_cast<u64>(end);
        }
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

    file_mode open_file::mode() const { return !_object ? file_mode{} : _object->mode(); }
    u64 open_file::mtime_sec() const { return !_object ? 0 : _object->mtime_sec(); }
    u64 open_file::mtime_nsec() const { return !_object ? 0 : _object->mtime_nsec(); }

} // namespace astia::fs
