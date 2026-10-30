#include "devfs.hpp"
#include "initrd_fs.hpp"
#include "tmpfs.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>
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

        // This policy is deliberately private to the registry.  The public
        // filesystem::mount ABI returns only a raw mount pointer, so cached
        // boot filesystems must not be mistaken for registry-owned objects.
        enum class mount_ownership : u8 {
            static_instance,
            registry_owned,
        };

        struct mount_entry {
            bool            used;
            char            target[path_max];
            mount*          instance;
            mount_ownership ownership;
            void (*destroy)(mount*);
            u32 active_operations;
        };

        type_entry     types[max_types];
        mount_entry    mounts[max_mounts];
        core::spinlock mount_registry_lock;
        bool           ready;
        bool           bootstrapping;

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
            u32 n = 0;
            while (value != nullptr && value[n] != '\0')
                ++n;
            return n;
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

        mount_entry* find_mount_locked(const char* path) {
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

        class mount_operation {
            public:
            explicit mount_operation(const char* path) {
                core::spinlock_guard guard(mount_registry_lock);
                entry_ = find_mount_locked(path);
                if (entry_ == nullptr || !entry_->used || entry_->instance == nullptr) {
                    entry_ = nullptr;
                    return;
                }
                ++entry_->active_operations;
                instance_ = entry_->instance;
            }
            ~mount_operation() {
                if (entry_ == nullptr)
                    return;
                core::spinlock_guard guard(mount_registry_lock);
                --entry_->active_operations;
            }

            mount_operation(const mount_operation&)            = delete;
            mount_operation& operator=(const mount_operation&) = delete;
            explicit         operator bool() const {
                return entry_ != nullptr;
            }
            mount_entry* entry() const {
                return entry_;
            }
            mount* instance() const {
                return instance_;
            }

            private:
            mount_entry* entry_    = nullptr;
            mount*       instance_ = nullptr;
        };

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

        void destroy_tmpfs_mount(mount* instance) {
            delete static_cast<tmpfs_mount*>(instance);
        }

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
    long mount::mkdir(const char*, file_mode) {
        return -ENOSYS;
    }
    long mount::unlink(const char*, bool) {
        return -ENOSYS;
    }
    long mount::rename(const char*, const char*) {
        return -ENOSYS;
    }
    long mount::link(const char*, const char*) {
        return -ENOSYS;
    }
    file_type mount::node_type(const char*) const {
        return directory_unknown;
    }
    file_mode mount::node_mode(const char*) const {
        return {};
    }
    uid_t mount::node_uid(const char*) const {
        return {};
    }
    gid_t mount::node_gid(const char*) const {
        return {};
    }
    uid_t file::uid() const {
        return {};
    }
    gid_t file::gid() const {
        return {};
    }
    long file::chown(uid_t, gid_t) {
        return -ENOSYS;
    }
    u64 file::inode() const {
        return 0;
    }
    u32 file::nlink() const {
        return 1;
    }
    u32 file::poll(u32 events, u32) {
        return events & (POLLIN | POLLOUT);
    }
    long mount::chmod(const char*, file_permissions) {
        return -ENOSYS;
    }
    long mount::chown(const char*, uid_t, gid_t) {
        return -ENOSYS;
    }
    long mount::symlink(const char*, const char*) {
        return -ENOSYS;
    }
    long mount::readlink(const char*, char*, u32) const {
        return -ENOSYS;
    }

    bool init() {
        for (u32 i = 0; i < max_types; ++i)
            types[i] = type_entry{};
        for (u32 i = 0; i < max_mounts; ++i)
            mounts[i] = mount_entry{};
        ready         = true;
        bootstrapping = true;

        devfs_instance = ::new (devfs_storage) devfs_mount{};
        devfs_type.attach(devfs_instance);
        const bool ok =
            register_type(&devfs_type) == 0 && register_type(&initrd_type()) == 0 &&
            register_type(&tmpfs_type_instance) == 0 &&
            mount_type("devfs", "/dev") == 0 && mount_type("tmpfs", "/tmp") == 0;
        bootstrapping = false;
        return ok;
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
        if (!ready || type_name == nullptr || target == nullptr || target[0] != '/')
            return -EINVAL;
        char checked_target[path_max]{};
        if (bootstrapping) {
            if (!copy_path(checked_target, target))
                return -ENAMETOOLONG;
        }
        else if (equal(target, "/")) {
            checked_target[0] = '/';
            checked_target[1] = '\0';
        }
        else {
            long rc = resolve_path(target, checked_target, sizeof checked_target, false);
            if (rc < 0) {
                if (!copy_path(checked_target, target))
                    return rc;
                bool known_mountpoint = false;
                {
                    core::spinlock_guard registry_guard(mount_registry_lock);
                    for (u32 i = 0; i < max_mounts; ++i)
                        if (equal(mounts[i].target, checked_target))
                            known_mountpoint = true;
                }
                if (!known_mountpoint)
                    return rc;
            }
            else if (node_type(checked_target) != directory_dir) {
                bool known_mountpoint = false;
                {
                    core::spinlock_guard registry_guard(mount_registry_lock);
                    for (u32 i = 0; i < max_mounts; ++i)
                        if (equal(mounts[i].target, checked_target))
                            known_mountpoint = true;
                }
                if (!known_mountpoint)
                    return node_type(checked_target) == directory_unknown ? -ENOENT
                                                                          : -ENOTDIR;
            }
        }
        const filesystem* type = find_type(type_name);
        if (type == nullptr)
            return -ENODEV;
        core::spinlock_guard registry_guard(mount_registry_lock);
        for (u32 i = 0; i < max_mounts; ++i) {
            if (mounts[i].used && equal(mounts[i].target, checked_target))
                return -EEXIST;
        }
        u32 free_slot = max_mounts;
        for (u32 i = 0; i < max_mounts; ++i) {
            if (!mounts[i].used) {
                free_slot = i;
                break;
            }
        }
        if (free_slot == max_mounts)
            return -ENFILE;
        mount* instance = type->mount(source);
        if (instance == nullptr)
            return -ENOMEM;

        // The filesystem() ABI deliberately returns only a mount pointer.  The
        // registry keeps the ownership policy private: tmpfs mounts are
        // registry-owned, while initrd and devfs are cached for the lifetime
        // of their filesystem
        // type.  They therefore use the explicit static marker too: the
        // registry cannot destroy an initrd without first clearing the
        // filesystem's cached pointer, and the public ABI has no teardown
        // hook for doing that safely.
        const mount_ownership ownership = type == &tmpfs_type_instance
                                              ? mount_ownership::registry_owned
                                              : mount_ownership::static_instance;
        void (*destroy)(mount*)         = nullptr;
        if (type == &tmpfs_type_instance)
            destroy = destroy_tmpfs_mount;
        if (!copy_path(mounts[free_slot].target, checked_target)) {
            if (destroy != nullptr)
                destroy(instance);
            return -ENAMETOOLONG;
        }
        mounts[free_slot].used      = true;
        mounts[free_slot].instance  = instance;
        mounts[free_slot].ownership = ownership;
        mounts[free_slot].destroy   = destroy;
        return 0;
    }

    long unmount_type(const char* target) {
        char checked_target[path_max]{};
        if (target == nullptr)
            return -EFAULT;
        if (equal(target, "/"))
            return -EBUSY;
        long resolve_result =
            resolve_path(target, checked_target, sizeof checked_target, false);
        if (resolve_result < 0)
            return resolve_result;
        if (equal(checked_target, "/"))
            return -EBUSY;
        core::spinlock_guard registry_guard(mount_registry_lock);
        mount_entry*         selected = nullptr;
        for (u32 i = 0; i < max_mounts; ++i) {
            if (mounts[i].used && equal(mounts[i].target, checked_target)) {
                selected = &mounts[i];
                break;
            }
        }
        if (selected == nullptr)
            return -ENOENT;
        if (selected->active_operations != 0 || selected->instance->open_count() != 0)
            return -EBUSY;
        for (u32 i = 0; i < max_mounts; ++i) {
            if (!mounts[i].used || &mounts[i] == selected)
                continue;
            if (path_matches(checked_target, mounts[i].target))
                return -EBUSY;
        }
        mount*                instance  = selected->instance;
        const mount_ownership ownership = selected->ownership;
        void (*destroy)(mount*)         = selected->destroy;
        selected->used                  = false;
        selected->instance              = nullptr;
        selected->ownership             = mount_ownership::static_instance;
        selected->destroy               = nullptr;
        // Only registry-owned factories may be destroyed here.  Cached devfs
        // has a null callback and remains in its static storage.
        if (ownership == mount_ownership::registry_owned && destroy != nullptr)
            destroy(instance);
        return 0;
    }

    long register_device(const char* path, const mm::shptr<file>& object) {
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        return operation.instance()->register_file(
            relative_path(*operation.entry(), path), object);
    }

    mm::shptr<open_file> open_path(const char* path, u32 flags) {
        char resolved[path_max]{};
        if (resolve_path(path, resolved, sizeof resolved, true) < 0)
            return {};
        mount_operation operation(resolved);
        if (!operation)
            return {};
        auto result = operation.instance()->open(
            relative_path(*operation.entry(), resolved), flags);
        if (result) {
            result->set_path(resolved);
            result->attach_mount(operation.instance());
        }
        return result;
    }

    mm::shptr<open_file> open(const char* path, u32 flags) {
        return open_path(path, flags);
    }

    mm::shptr<open_file> open_directory(const char* path) {
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, true);
        if (rc < 0 || node_type(resolved) != directory_dir)
            return {};
        mount_operation operation(resolved);
        if (!operation)
            return {};
        auto result = mm::share<open_file>(resolved, node_mode(resolved));
        if (result)
            result->attach_mount(operation.instance());
        return result;
    }

    long read_directory(const char* path, u64 index, char* name, u32 capacity,
                        file_type* type) {
        if (path == nullptr || name == nullptr || type == nullptr || capacity == 0)
            return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, true);
        if (rc < 0)
            return rc;
        mount_operation operation(resolved);
        if (!operation)
            return -ENOENT;
        return operation.instance()->read_directory(
            relative_path(*operation.entry(), resolved), index, name, capacity, type);
    }

    long make_directory(const char* path, file_mode mode) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0)
            return rc;
        mount_operation operation(resolved);
        if (!operation)
            return -ENOENT;
        return operation.instance()->mkdir(relative_path(*operation.entry(), resolved),
                                           mode);
    }

    long remove_path(const char* path, bool directory) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0)
            return rc;
        mount_operation operation(resolved);
        if (!operation)
            return -ENOENT;
        return operation.instance()->unlink(relative_path(*operation.entry(), resolved),
                                            directory);
    }

    long rename_path(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        char old_resolved[path_max]{}, new_resolved[path_max]{};
        long rc = resolve_path(old_path, old_resolved, sizeof old_resolved, false);
        if (rc < 0)
            return rc;
        rc = resolve_path(new_path, new_resolved, sizeof new_resolved, false);
        if (rc < 0)
            return rc;
        mount_operation old_operation(old_resolved);
        mount_operation new_operation(new_resolved);
        if (!old_operation || !new_operation)
            return -ENOENT;
        if (old_operation.instance() != new_operation.instance())
            return -EXDEV;
        return old_operation.instance()->rename(
            relative_path(*old_operation.entry(), old_resolved),
            relative_path(*new_operation.entry(), new_resolved));
    }

    long link_path(const char* old_path, const char* new_path) {
        if (old_path == nullptr || new_path == nullptr || old_path[0] != '/' ||
            new_path[0] != '/')
            return -EINVAL;
        char old_resolved[path_max]{}, new_resolved[path_max]{};
        long rc = resolve_path(old_path, old_resolved, sizeof old_resolved, true);
        if (rc < 0)
            return rc;
        rc = resolve_path(new_path, new_resolved, sizeof new_resolved, false);
        if (rc < 0)
            return rc;
        mount_operation old_operation(old_resolved);
        mount_operation new_operation(new_resolved);
        if (!old_operation || !new_operation)
            return -ENOENT;
        if (old_operation.instance() != new_operation.instance())
            return -EXDEV;
        return old_operation.instance()->link(
            relative_path(*old_operation.entry(), old_resolved),
            relative_path(*new_operation.entry(), new_resolved));
    }

    file_type node_type(const char* path) {
        if (path == nullptr || path[0] != '/')
            return directory_unknown;
        mount_operation operation(path);
        if (!operation)
            return directory_unknown;
        mount_entry* entry = operation.entry();
        // A mount point is itself a directory in the namespace.  Some
        // filesystems (notably devfs) only expose entries below their root
        // and therefore cannot describe the mount root through node_type().
        if (equal(entry->target, path))
            return directory_dir;
        return operation.instance()->node_type(relative_path(*entry, path));
    }

    long resolve_path(const char* path, char* buffer, u32 capacity, bool follow_final) {
        if (path == nullptr || buffer == nullptr || capacity == 0 || path[0] != '/')
            return -EINVAL;
        char pending[path_max * 2]{};
        char result[path_max]{};
        u32  plen = 0, rlen = 1, links = 0;
        u32  path_length = 0;
        while (path[path_length] != '\0')
            ++path_length;
        const bool trailing_slash = path_length != 0 && path[path_length - 1] == '/';
        result[0]                 = '/';
        for (u32 i = 0; path[i] != '\0'; ++i) {
            if (plen + 1 >= sizeof pending)
                return -ENAMETOOLONG;
            pending[plen++] = path[i];
        }
        pending[plen] = '\0';
        for (;;) {
            u32 start = 0;
            while (start < plen && pending[start] == '/')
                ++start;
            if (start == plen)
                break;
            u32 end = start;
            while (end < plen && pending[end] != '/')
                ++end;
            char component[path_max]{};
            u32  clen = end - start;
            if (clen >= path_max)
                return -ENAMETOOLONG;
            for (u32 i = 0; i < clen; ++i)
                component[i] = pending[start + i];
            component[clen] = '\0';
            for (u32 i = end; i <= plen; ++i)
                pending[i - end] = pending[i];
            plen -= end;
            while (plen != 0 && pending[0] == '/') {
                for (u32 i = 0; i < plen; ++i)
                    pending[i] = pending[i + 1];
                --plen;
            }
            if (equal(component, "."))
                continue;
            if (equal(component, "..")) {
                if (rlen > 1) {
                    --rlen;
                    while (rlen > 1 && result[rlen - 1] != '/')
                        --rlen;
                }
                continue;
            }
            // Every component except the root is looked up beneath a
            // directory.  Reporting ENOTDIR here is important: treating an
            // ordinary file as an absent path loses the caller's errno.
            if (rlen > 1 && node_type(result) != directory_dir)
                return node_type(result) == directory_unknown ? -ENOENT : -ENOTDIR;
            const u32 parent_len = rlen;
            if (rlen > 1)
                result[rlen++] = '/';
            if (rlen + clen >= sizeof result)
                return -ENAMETOOLONG;
            for (u32 i = 0; i < clen; ++i)
                result[rlen++] = component[i];
            result[rlen]    = '\0';
            const bool last = plen == 0;
            if ((!last || follow_final) && node_type(result) == directory_lnk) {
                if (++links > 16)
                    return -ELOOP;
                char target[path_max]{};
                long n = read_symlink(result, target, sizeof target);
                if (n < 0)
                    return n;
                char combined[path_max * 2]{};
                u32  at = 0;
                if (target[0] != '/') {
                    at = parent_len;
                    for (u32 i = 0; i < at; ++i)
                        combined[i] = result[i];
                    if (at > 1 && combined[at - 1] != '/')
                        combined[at++] = '/';
                }
                for (u32 i = 0; target[i] != '\0'; ++i) {
                    if (at + i + 1 >= sizeof combined)
                        return -ENAMETOOLONG;
                    combined[at + i] = target[i];
                }
                u32 tail = 0;
                while (pending[tail] != '\0')
                    ++tail;
                const u32 target_len   = length_of(target);
                const u32 combined_len = at + target_len + (tail ? tail + 1 : 0);
                if (combined_len >= sizeof combined)
                    return -ENAMETOOLONG;
                if (tail != 0) {
                    combined[at + target_len] = '/';
                    for (u32 i = 0; i <= tail; ++i)
                        combined[at + target_len + 1 + i] = pending[i];
                }
                else {
                    combined[combined_len] = '\0';
                }
                plen = combined_len;
                for (u32 i = 0; i <= plen; ++i)
                    pending[i] = combined[i];
                rlen      = 1;
                result[0] = '/';
            }
        }
        if (trailing_slash) {
            const file_type final_type = node_type(result);
            if (final_type == directory_unknown)
                return -ENOENT;
            if (final_type != directory_dir)
                return -ENOTDIR;
        }
        // rlen excludes the terminating NUL; leave room for it in the
        // caller's buffer before copying the complete string below.
        if (rlen + 1 > capacity)
            return -ENAMETOOLONG;
        for (u32 i = 0; i <= rlen; ++i)
            buffer[i] = result[i];
        return 0;
    }

    long make_symlink(const char* target, const char* path) {
        if (target == nullptr || path == nullptr || path[0] != '/')
            return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0)
            return rc;
        mount_operation operation(resolved);
        if (!operation)
            return -ENOENT;
        return operation.instance()->symlink(target,
                                             relative_path(*operation.entry(), resolved));
    }
    long read_symlink(const char* path, char* buffer, u32 capacity) {
        if (path == nullptr || path[0] != '/' || buffer == nullptr || capacity == 0)
            return -EINVAL;
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        return operation.instance()->readlink(relative_path(*operation.entry(), path),
                                              buffer, capacity);
    }

    file_mode node_mode(const char* path) {
        if (path == nullptr || path[0] != '/')
            return {};
        mount_operation operation(path);
        return !operation ? file_mode{}
                          : operation.instance()->node_mode(
                                relative_path(*operation.entry(), path));
    }
    uid_t node_uid(const char* path) {
        mount_operation operation(path);
        return operation ? operation.instance()->node_uid(
                               relative_path(*operation.entry(), path))
                         : uid_t{};
    }
    gid_t node_gid(const char* path) {
        mount_operation operation(path);
        return operation ? operation.instance()->node_gid(
                               relative_path(*operation.entry(), path))
                         : gid_t{};
    }

    long chmod_path(const char* path, file_permissions permissions) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        return operation.instance()->chmod(relative_path(*operation.entry(), path),
                                           permissions);
    }

    long chown_path(const char* path, uid_t uid, gid_t gid, bool follow_final) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, follow_final);
        if (rc < 0)
            return rc;
        mount_operation operation(resolved);
        return !operation ? -ENOENT
                          : operation.instance()->chown(
                                relative_path(*operation.entry(), resolved), uid, gid);
    }

    long touch_path(const char* path, u64 sec, u64 nsec) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        auto object =
            operation.instance()->open(relative_path(*operation.entry(), path), 0);
        if (!object)
            return -ENOENT;
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
    long file::ioctl(u64 request, void* argp, usize size) {
        (void) request;
        (void) argp;
        (void) size;
        return -ENOTTY;
    }

    long file::size() const {
        return -ESPIPE;
    }

    file_mode file::mode() const {
        return file_mode(file_type::regular, file_permissions(0666));
    }
    u64 file::mtime_sec() const {
        return 0;
    }
    u64 file::mtime_nsec() const {
        return 0;
    }
    long file::touch(u64, u64) {
        return -EROFS;
    }

    mm::phys_region* file::memory_region() {
        return nullptr;
    }
    void file::duplicate(u32) {}
    void file::close(u32) {}

    namespace {
        long legacy_read(file* object, void* buffer, u32 length, u64 offset) {
            return object->read(buffer, length, offset);
        }
        long legacy_write(file* object, const void* buffer, u32 length, u64 offset) {
            return object->write(buffer, length, offset);
        }
        long legacy_submit(file* object, io_request& request) {
            return object->submit(request);
        }
        u32 legacy_poll(const file* object, u32 events, u32 flags) {
            return const_cast<file*>(object)->poll(events, flags);
        }
        long legacy_ioctl(file* object, u64 request, void* argp, usize size) {
            return object->ioctl(request, argp, size);
        }
        long legacy_size(const file* object) {
            return object->size();
        }
        mm::phys_region* legacy_mapping(const file* object) {
            return const_cast<file*>(object)->memory_region();
        }

        const file_read_capability    legacy_read_capability{legacy_read};
        const file_write_capability   legacy_write_capability{legacy_write};
        const file_submit_capability  legacy_submit_capability{legacy_submit};
        const file_poll_capability    legacy_poll_capability{legacy_poll};
        const file_ioctl_capability   legacy_ioctl_capability{legacy_ioctl};
        const file_size_capability    legacy_size_capability{legacy_size};
        const file_mapping_capability legacy_mapping_capability{legacy_mapping};

        long dispatch_submit(file* object, io_request& request) {
            const auto* capability = static_cast<const file_submit_capability*>(
                object->capability(file_capability::submit));
            return capability != nullptr ? capability->submit(object, request)
                                         : object->submit(request);
        }

        u32 dispatch_poll(const file* object, u32 events, u32 flags) {
            const auto* capability = static_cast<const file_poll_capability*>(
                object->capability(file_capability::poll));
            return capability != nullptr ? capability->poll(object, events, flags)
                                         : const_cast<file*>(object)->poll(events, flags);
        }

        long dispatch_size(const file* object) {
            const auto* capability = static_cast<const file_size_capability*>(
                object->capability(file_capability::size));
            return capability != nullptr ? capability->size(object) : object->size();
        }

    }

    long dispatch_read(file* object, void* buffer, u32 length, u64 offset) {
        const auto* capability = static_cast<const file_read_capability*>(
            object->capability(file_capability::read));
        return capability != nullptr ? capability->read(object, buffer, length, offset)
                                     : object->read(buffer, length, offset);
    }

    long dispatch_write(file* object, const void* buffer, u32 length, u64 offset) {
        const auto* capability = static_cast<const file_write_capability*>(
            object->capability(file_capability::write));
        return capability != nullptr ? capability->write(object, buffer, length, offset)
                                     : object->write(buffer, length, offset);
    }

    long dispatch_ioctl(file* object, u64 request, void* argp, usize size) {
        const auto* capability = static_cast<const file_ioctl_capability*>(
            object->capability(file_capability::ioctl));
        return capability != nullptr ? capability->ioctl(object, request, argp, size)
                                     : object->ioctl(request, argp, size);
    }

    mm::phys_region* dispatch_mapping(const file* object) {
        const auto* capability = static_cast<const file_mapping_capability*>(
            object->capability(file_capability::mapping));
        return capability != nullptr ? capability->mapping(object)
                                     : const_cast<file*>(object)->memory_region();
    }

    const void* file::capability(file_capability kind) const {
        switch (kind) {
        case file_capability::read:
            return &legacy_read_capability;
        case file_capability::write:
            return &legacy_write_capability;
        case file_capability::submit:
            return &legacy_submit_capability;
        case file_capability::poll:
            return &legacy_poll_capability;
        case file_capability::ioctl:
            return &legacy_ioctl_capability;
        case file_capability::size:
            return &legacy_size_capability;
        case file_capability::mapping:
            return &legacy_mapping_capability;
        }
        return nullptr;
    }

    const unix_socket_operations* file::unix_socket_capability() const {
        return nullptr;
    }

    open_file::~open_file() {
        if (_mount)
            _mount->release_open();
        if (_object)
            _object->close(_flags);
    }

    void open_file::attach_mount(mount* owner) {
        if (_mount == owner)
            return;
        if (_mount)
            _mount->release_open();
        _mount = owner;
        if (_mount)
            _mount->retain_open();
    }

    long open_file::read(void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        const u64  actual = offset == offset_current ? _position : offset;
        const long result = dispatch_read(_object.get(), buf, len, actual);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    long open_file::write(const void* buf, u32 len, u64 offset) {
        if (!_object)
            return -EBADF;
        u64 actual = offset == offset_current ? _position : offset;
        if ((_flags & static_cast<u32>(open_file_status_flags::append)) != 0) {
            const long end = dispatch_size(_object.get());
            if (end < 0)
                return end;
            actual = static_cast<u64>(end);
        }
        const long result = dispatch_write(_object.get(), buf, len, actual);
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
        if ((_flags & static_cast<u32>(open_file_status_flags::append)) != 0 &&
            request.direction() == io_direction::output) {
            const long end = dispatch_size(_object.get());
            if (end < 0)
                return end;
            actual = static_cast<u64>(end);
        }
        request.offset(actual);
        const long result = dispatch_submit(_object.get(), request);
        if (result > 0)
            _position = actual + static_cast<u64>(result);
        return result;
    }
    u32 open_file::poll(u32 events) const {
        if (!_object)
            return static_cast<u32>(POLLNVAL);
        return dispatch_poll(_object.get(), events, _flags);
    }
    long open_file::ioctl(u64 request, void* argp, usize size) {
        if (!_object)
            return -EBADF;
        return dispatch_ioctl(_object.get(), request, argp, size);
    }

    long open_file::size() const {
        if (!_object)
            return -EBADF;
        return dispatch_size(_object.get());
    }
    mm::phys_region* open_file::mapping() const {
        return _object ? dispatch_mapping(_object.get()) : nullptr;
    }

    file_mode open_file::mode() const {
        return !_object ? _mode : _object->mode();
    }
    void open_file::set_path(const char* path) {
        if (path == nullptr)
            return;
        u32 i = 0;
        for (; i + 1 < path_max && path[i] != '\0'; ++i)
            _path[i] = path[i];
        _path[i]  = '\0';
        _has_path = true;
    }
    u64 open_file::mtime_sec() const {
        return !_object ? 0 : _object->mtime_sec();
    }
    u64 open_file::mtime_nsec() const {
        return !_object ? 0 : _object->mtime_nsec();
    }

} // namespace astia::fs
