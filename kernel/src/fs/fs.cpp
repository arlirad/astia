#include <astia/api/errno.h>
#include <astia/core/rcu_vector.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/fs.hpp>
#include <astia/mm/share.hpp>

#include "devfs.hpp"
#include "ext4/ext4.hpp"
#include "initrd_fs.hpp"
#include "path_service.hpp"
#include "tmpfs.hpp"
#include "unix_pathname.hpp"

void* operator new(astia::usize size, void* where) noexcept;

namespace astia::fs {

    namespace {
        inline constexpr u32 max_types  = 16;
        inline constexpr u32 max_mounts = 16;

        struct type_entry {
            const filesystem* type              = nullptr;
            u32               generation        = 0;
            u32               active_operations = 0;
            u32               mount_count       = 0;
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
            u32         active_operations;
            type_entry* owner;
            u32         owner_generation;
        };

        struct mount_snapshot_entry {
            bool used  = false;
            u32  index = 0;
            char target[path_max]{};
        };

        type_entry                                         types[max_types];
        mount_entry                                        mounts[max_mounts];
        core::rcu_vector<mount_snapshot_entry, max_mounts> mount_snapshot;
        core::spinlock                                     mount_registry_lock;
        // Lookup callers still take mount_registry_lock.  This is only a
        // hint: the generation makes the entry safe to reuse after an
        // unmount or a later mount fills the same slot.
        struct lookup_hint {
            char         path[path_max];
            mount_entry* entry;
            u64          generation;
        };
        lookup_hint mount_lookup_hint{};
        u64         mount_registry_generation;
        bool        ready;
        bool        bootstrapping;

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

        type_entry* find_type_locked(const char* name) {
            for (u32 i = 0; i < max_types; ++i) {
                if (types[i].type != nullptr && equal(types[i].type->name(), name)) {
                    return &types[i];
                }
            }
            return nullptr;
        }

        class type_operation {
            public:
            explicit type_operation(const char* name) {
                core::spinlock_guard guard(mount_registry_lock);
                _entry = find_type_locked(name);
                if (_entry == nullptr)
                    return;
                ++_entry->active_operations;
                _instance = _entry->type;
            }

            explicit type_operation(type_entry* entry) {
                core::spinlock_guard guard(mount_registry_lock);
                if (entry == nullptr || entry->type == nullptr)
                    return;
                ++entry->active_operations;
                _entry    = entry;
                _instance = entry->type;
            }

            ~type_operation() {
                if (_entry == nullptr)
                    return;
                core::spinlock_guard guard(mount_registry_lock);
                --_entry->active_operations;
            }

            type_operation(const type_operation&)            = delete;
            type_operation& operator=(const type_operation&) = delete;

            explicit operator bool() const {
                return _entry != nullptr;
            }
            const filesystem* instance() const {
                return _instance;
            }
            type_entry* owner() const {
                return _entry;
            }

            private:
            type_entry*       _entry    = nullptr;
            const filesystem* _instance = nullptr;
        };

        bool path_matches(const char* target, const char* path) {
            if (equal(target, "/")) {
                return path != nullptr && path[0] == '/';
            }
            u32 i = 0;
            while (target[i] != '\0' && target[i] == path[i])
                ++i;
            return target[i] == '\0' && (path[i] == '\0' || path[i] == '/');
        }

        void publish_mount_snapshot_locked() {
            mount_snapshot_entry entries[max_mounts]{};
            for (u32 i = 0; i < max_mounts; ++i) {
                entries[i].used  = mounts[i].used;
                entries[i].index = i;
                if (entries[i].used)
                    copy_path(entries[i].target, mounts[i].target);
            }
            (void) mount_snapshot.replace(
                span<const mount_snapshot_entry>(entries, max_mounts));
        }

        u32 find_mount_snapshot_index(const char* path) {
            u32 best        = max_mounts;
            u32 best_length = 0;
            core::rcu_vector<mount_snapshot_entry, max_mounts>::read_guard snapshot(
                mount_snapshot);
            for (usize i = 0; i < snapshot.size(); ++i) {
                const auto& candidate = snapshot[i];
                if (!candidate.used || !path_matches(candidate.target, path))
                    continue;
                const u32 length = length_of(candidate.target);
                if (best == max_mounts || length > best_length) {
                    best        = candidate.index;
                    best_length = length;
                }
            }
            return best;
        }

        mount_entry* find_mount_locked(const char* path) {
            mount_entry* best     = nullptr;
            u32          best_len = 0;
            {
                core::rcu_vector<mount_snapshot_entry, max_mounts>::read_guard snapshot(
                    mount_snapshot);
                for (usize i = 0; i < snapshot.size(); ++i) {
                    const auto& candidate = snapshot[i];
                    if (!candidate.used || !path_matches(candidate.target, path))
                        continue;
                    u32 length = length_of(candidate.target);
                    if (best == nullptr || length > best_len) {
                        best     = candidate.index < max_mounts ? &mounts[candidate.index]
                                                                : nullptr;
                        best_len = length;
                    }
                }
            }
            if (best != nullptr && best->used && best->instance != nullptr &&
                path_matches(best->target, path))
                return best;

            // The snapshot is only an optimization.  A failed publication or
            // a stale slot is handled by the authoritative registry scan.
            if (mount_lookup_hint.generation == mount_registry_generation &&
                mount_lookup_hint.entry != nullptr && mount_lookup_hint.entry->used &&
                mount_lookup_hint.entry->instance != nullptr &&
                equal(mount_lookup_hint.path, path))
                return mount_lookup_hint.entry;

            best     = nullptr;
            best_len = 0;
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
            // Do not retain a caller-owned path.  Overlong paths are not
            // cacheable, but remain fully handled by the normal scan.
            if (copy_path(mount_lookup_hint.path, path)) {
                mount_lookup_hint.entry      = best;
                mount_lookup_hint.generation = mount_registry_generation;
            }
            return best;
        }

        class mount_operation {
            public:
            explicit mount_operation(const char* path) {
                const u32            snapshot_index = find_mount_snapshot_index(path);
                core::spinlock_guard guard(mount_registry_lock);
                _entry = snapshot_index < max_mounts && mounts[snapshot_index].used &&
                                 mounts[snapshot_index].instance != nullptr &&
                                 path_matches(mounts[snapshot_index].target, path)
                             ? &mounts[snapshot_index]
                             : find_mount_locked(path);
                if (_entry == nullptr || !_entry->used || _entry->instance == nullptr) {
                    _entry = nullptr;
                    return;
                }
                ++_entry->active_operations;
                _instance = _entry->instance;
            }
            ~mount_operation() {
                if (_entry == nullptr)
                    return;
                core::spinlock_guard guard(mount_registry_lock);
                --_entry->active_operations;
            }

            mount_operation(const mount_operation&)            = delete;
            mount_operation& operator=(const mount_operation&) = delete;
            explicit         operator bool() const {
                return _entry != nullptr;
            }
            mount_entry* entry() const {
                return _entry;
            }
            mount* instance() const {
                return _instance;
            }

            private:
            mount_entry* _entry    = nullptr;
            mount*       _instance = nullptr;
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

    } // namespace

    mm::shptr<open_file> mount::open(const char* path, u32 flags) {
        (void) path;
        (void) flags;
        return {};
    }

    long mount::register_file(const char* path, const mm::shptr<file>& entry) {
        (void) path;
        (void) entry;
        return -ENOSYS;
    }

    long mount::unregister_file(const char* path, const mm::shptr<file>& entry) {
        (void) path;
        (void) entry;
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
    long file::chmod(file_permissions) {
        return -ENOSYS;
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
        publish_mount_snapshot_locked();
        mount_lookup_hint         = lookup_hint{};
        mount_registry_generation = 0;
        ready                     = true;
        bootstrapping             = true;

        devfs_instance = ::new (devfs_storage) devfs_mount{};
        devfs_type.attach(devfs_instance);
        const bool ok =
            register_type(&devfs_type) == 0 && register_type(&initrd_type()) == 0 &&
            register_type(&ext4_type()) == 0 &&
            register_type(&tmpfs_type_instance) == 0 &&
            mount_type("devfs", "/dev") == 0 && mount_type("tmpfs", "/tmp") == 0;
        bootstrapping = false;
        return ok;
    }

    long register_type(const filesystem* type) {
        filesystem_registration registration;
        return register_type(type, registration);
    }

    long register_type(const filesystem* type, filesystem_registration& registration) {
        if (!ready || type == nullptr || type->name() == nullptr || registration.valid())
            return -EINVAL;

        core::spinlock_guard guard(mount_registry_lock);
        const char*          name = type->name();
        for (u32 i = 0; i < max_types; ++i) {
            if (types[i].type != nullptr && equal(types[i].type->name(), name))
                return -EEXIST;
        }
        for (u32 i = 0; i < max_types; ++i) {
            type_entry& entry = types[i];
            if (entry.type != nullptr)
                continue;
            entry.type       = type;
            entry.generation = entry.generation + 1 == 0 ? 1 : entry.generation + 1;
            entry.active_operations = 0;
            entry.mount_count       = 0;
            registration            = {i, entry.generation};
            return 0;
        }
        return -ENFILE;
    }

    long unregister_type(filesystem_registration& registration) {
        if (!registration.valid() || registration.slot >= max_types)
            return -EINVAL;

        core::spinlock_guard guard(mount_registry_lock);
        type_entry&          entry = types[registration.slot];
        if (entry.type == nullptr || entry.generation != registration.generation)
            return -ENOENT;
        if (entry.active_operations != 0 || entry.mount_count != 0)
            return -EBUSY;
        entry.type              = nullptr;
        entry.active_operations = 0;
        entry.mount_count       = 0;
        registration            = filesystem_registration{};
        return 0;
    }

    long unregister_type(const filesystem* type) {
        if (type == nullptr)
            return -EINVAL;

        core::spinlock_guard guard(mount_registry_lock);
        type_entry*          entry = nullptr;
        for (u32 i = 0; i < max_types; ++i) {
            if (types[i].type == type) {
                entry = &types[i];
                break;
            }
        }
        if (entry == nullptr)
            return -ENOENT;
        if (entry->active_operations != 0 || entry->mount_count != 0)
            return -EBUSY;
        entry->type              = nullptr;
        entry->active_operations = 0;
        entry->mount_count       = 0;
        return 0;
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
        type_operation type_operation_guard(type_name);
        if (!type_operation_guard)
            return -ENODEV;
        const filesystem* type = type_operation_guard.instance();

        // Mount factories may allocate and initialize an entire filesystem.
        // Do that before taking the registry lock; publication below is the
        // only part that needs to be serialized.  The duplicate and capacity
        // checks are repeated there because another mount may be published
        // while the factory is running.
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
        const mount_ownership ownership = type->owns_mount()
                                              ? mount_ownership::registry_owned
                                              : mount_ownership::static_instance;

        bool published     = false;
        long publish_error = -ENFILE;
        {
            core::spinlock_guard registry_guard(mount_registry_lock);
            for (u32 i = 0; i < max_mounts; ++i) {
                if (mounts[i].used && equal(mounts[i].target, checked_target)) {
                    publish_error = -EEXIST;
                    break;
                }
            }
            if (publish_error == -ENFILE) {
                u32 free_slot = max_mounts;
                for (u32 i = 0; i < max_mounts; ++i) {
                    if (!mounts[i].used) {
                        free_slot = i;
                        break;
                    }
                }
                if (free_slot != max_mounts &&
                    copy_path(mounts[free_slot].target, checked_target)) {
                    mounts[free_slot].used              = true;
                    mounts[free_slot].instance          = instance;
                    mounts[free_slot].ownership         = ownership;
                    mounts[free_slot].destroy           = nullptr;
                    mounts[free_slot].active_operations = 0;
                    mounts[free_slot].owner             = type_operation_guard.owner();
                    mounts[free_slot].owner_generation =
                        type_operation_guard.owner()->generation;
                    ++type_operation_guard.owner()->mount_count;
                    ++mount_registry_generation;
                    publish_mount_snapshot_locked();
                    published = true;
                }
            }
        }

        if (!published) {
            if (ownership == mount_ownership::registry_owned)
                type->destroy_mount(instance);
            return publish_error;
        }
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
        mount*            instance        = nullptr;
        mount_ownership   ownership       = mount_ownership::static_instance;
        const filesystem* destroyer       = nullptr;
        type_entry*       owner           = nullptr;
        bool              owner_operation = false;
        {
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
            instance  = selected->instance;
            ownership = selected->ownership;
            owner     = selected->owner;
            destroyer = owner != nullptr ? owner->type : nullptr;
            if (owner != nullptr && owner->generation == selected->owner_generation) {
                ++owner->active_operations;
                if (owner->mount_count != 0)
                    --owner->mount_count;
                owner_operation = true;
            }
            selected->used             = false;
            selected->instance         = nullptr;
            selected->ownership        = mount_ownership::static_instance;
            selected->destroy          = nullptr;
            selected->owner            = nullptr;
            selected->owner_generation = 0;
            ++mount_registry_generation;
            publish_mount_snapshot_locked();
        }
        // Only registry-owned factories may be destroyed here.  Cached devfs
        // has a null callback and remains in its static storage.
        if (ownership == mount_ownership::registry_owned && destroyer != nullptr)
            destroyer->destroy_mount(instance);
        if (owner_operation) {
            core::spinlock_guard registry_guard(mount_registry_lock);
            --owner->active_operations;
        }
        return 0;
    }

    mount* retain_mount_for_unix_path(const char* canonical) {
        if (!canonical)
            return nullptr;
        core::spinlock_guard guard(mount_registry_lock);
        mount_entry*         entry = find_mount_locked(canonical);
        if (!entry || !entry->used || !entry->instance)
            return nullptr;
        entry->instance->retain_open();
        return entry->instance;
    }

    void release_mount_reference(mount* owner) {
        if (owner)
            owner->release_open();
    }

    long register_device(const char* path, const mm::shptr<file>& entry) {
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        return operation.instance()->register_file(
            relative_path(*operation.entry(), path), entry);
    }

    long unregister_device(const char* path, const mm::shptr<file>& entry) {
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        return operation.instance()->unregister_file(
            relative_path(*operation.entry(), path), entry);
    }

    mm::shptr<open_file> open_path(const char* path, u32 flags) {
        char resolved[path_max]{};
        if (resolve_path(path, resolved, sizeof resolved, true) < 0)
            return {};
        mount_operation operation(resolved);
        if (!operation)
            return {};
        mm::shptr<file> unix_endpoint;
        if (lookup_unix_path(resolved, unix_endpoint) == 0) {
            auto result = mm::share<open_file>(unix_endpoint, flags);
            if (result) {
                result->set_path(resolved);
                result->attach_mount(operation.instance());
            }
            return result;
        }
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
        auto result = operation.instance()->open(
            relative_path(*operation.entry(), resolved), ASTIA_AIO_O_DIRECTORY);
        if (!result)
            result = mm::share<open_file>(resolved, node_mode(resolved));
        if (result)
            result->set_path(resolved);
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
        const char* relative = relative_path(*operation.entry(), resolved);
        rc = operation.instance()->read_directory(relative, index, name, capacity, type);
        if (rc > 0)
            return rc;
        u64 base = 0;
        while (operation.instance()->read_directory(relative, base, name, capacity,
                                                    type) > 0)
            ++base;
        return read_unix_directory(resolved, operation.instance(), index - base,
                                   astia::span<char>(name, capacity), type);
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
        long unix_rc = remove_unix_path(resolved, operation.instance(), directory);
        if (unix_rc != 0)
            return unix_rc > 0 ? 0 : unix_rc;
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
        long unix_rc =
            rename_unix_path(old_resolved, new_resolved, old_operation.instance());
        if (unix_rc != 0)
            return unix_rc > 0 ? 0 : unix_rc;
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
        if (unix_node_type(old_resolved, old_operation.instance()) != directory_unknown)
            return -EPERM;
        if (unix_node_type(new_resolved, new_operation.instance()) != directory_unknown)
            return -EEXIST;
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
        file_type unix_type = unix_node_type(path, operation.instance());
        if (unix_type != directory_unknown)
            return unix_type;
        return operation.instance()->node_type(relative_path(*entry, path));
    }

    long resolve_path(const char* path, char* buffer, u32 capacity, bool follow_final) {
        return path_service::resolve(path, buffer, capacity, follow_final);
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
        if (!operation)
            return {};
        file_mode unix_mode = unix_node_mode(path, operation.instance());
        return unix_mode.exists() ? unix_mode
                                  : operation.instance()->node_mode(
                                        relative_path(*operation.entry(), path));
    }
    uid_t node_uid(const char* path) {
        mount_operation operation(path);
        if (!operation)
            return {};
        file_type unix_type = unix_node_type(path, operation.instance());
        return unix_type != directory_unknown
                   ? unix_node_uid(path, operation.instance())
                   : operation.instance()->node_uid(
                         relative_path(*operation.entry(), path));
    }
    gid_t node_gid(const char* path) {
        mount_operation operation(path);
        if (!operation)
            return {};
        file_type unix_type = unix_node_type(path, operation.instance());
        return unix_type != directory_unknown
                   ? unix_node_gid(path, operation.instance())
                   : operation.instance()->node_gid(
                         relative_path(*operation.entry(), path));
    }

    long chmod_path(const char* path, file_permissions permissions) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        long unix_rc = unix_chmod_path(path, operation.instance(), permissions);
        if (unix_rc != -ENOENT)
            return unix_rc;
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
        if (!operation)
            return -ENOENT;
        long unix_rc = unix_chown_path(resolved, operation.instance(), uid, gid);
        if (unix_rc != -ENOENT)
            return unix_rc;
        return operation.instance()->chown(relative_path(*operation.entry(), resolved),
                                           uid, gid);
    }

    long touch_path(const char* path, u64 sec, u64 nsec) {
        if (path == nullptr || path[0] != '/')
            return -EINVAL;
        mount_operation operation(path);
        if (!operation)
            return -ENOENT;
        auto handle =
            operation.instance()->open(relative_path(*operation.entry(), path), 0);
        if (!handle)
            return -ENOENT;
        return handle->parent()->touch(sec, nsec);
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
    long file::submit(io::request& request) {
        // Synchronous default: a node that finishes inline answers with
        // whatever its read()/write() returns.  A 0 from it here is not the
        // device contract's "park me" — a file has no device to resume it,
        // and end of file is exactly a completion with nothing moved — so it
        // is marked complete before the engine can mistake the two and wait
        // for a resume that never comes.  A node that genuinely can be "not
        // ready" overrides submit(), parks, and resumes by stage instead.
        const long result =
            request.direction() == io::direction::input
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

    long file::acquire_controlling_terminal(i32) {
        return -ENOTTY;
    }

    long file::size() const {
        return -ESPIPE;
    }
    u32 file::block_size() const {
        return 0;
    }

    long file::truncate(u64) {
        return -EINVAL;
    }

    long file::sync() {
        return -EINVAL;
    }

    long file::sync_mapping(mm::phys_region*, u64, u64, u32) {
        return -ENOSYS;
    }

    file_mode file::mode() const {
        return file_mode(file_type::regular, default_file_permissions);
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
        long legacy_read(file* parent, void* buffer, u32 length, u64 offset) {
            return parent->read(buffer, length, offset);
        }
        long legacy_write(file* parent, const void* buffer, u32 length, u64 offset) {
            return parent->write(buffer, length, offset);
        }
        long legacy_submit(file* parent, io::request& request) {
            return parent->submit(request);
        }
        u32 legacy_poll(const file* parent, u32 events, u32 flags) {
            return const_cast<file*>(parent)->poll(events, flags);
        }
        long legacy_ioctl(file* parent, u64 request, void* argp, usize size) {
            return parent->ioctl(request, argp, size);
        }
        long legacy_size(const file* parent) {
            return parent->size();
        }
        mm::phys_region* legacy_mapping(const file* parent) {
            return const_cast<file*>(parent)->memory_region();
        }

        const file_read_capability    legacy_read_capability{legacy_read};
        const file_write_capability   legacy_write_capability{legacy_write};
        const file_submit_capability  legacy_submit_capability{legacy_submit};
        const file_poll_capability    legacy_poll_capability{legacy_poll};
        const file_ioctl_capability   legacy_ioctl_capability{legacy_ioctl};
        const file_size_capability    legacy_size_capability{legacy_size};
        const file_mapping_capability legacy_mapping_capability{legacy_mapping};

        long dispatch_submit(file* parent, io::request& request) {
            const auto* capability = static_cast<const file_submit_capability*>(
                parent->capability(file_capability::submit));
            return capability != nullptr ? capability->submit(parent, request)
                                         : parent->submit(request);
        }

        u32 dispatch_poll(const file* parent, u32 events, u32 flags) {
            const auto* capability = static_cast<const file_poll_capability*>(
                parent->capability(file_capability::poll));
            return capability != nullptr ? capability->poll(parent, events, flags)
                                         : const_cast<file*>(parent)->poll(events, flags);
        }

        long dispatch_size(const file* parent) {
            const auto* capability = static_cast<const file_size_capability*>(
                parent->capability(file_capability::size));
            return capability != nullptr ? capability->size(parent) : parent->size();
        }

    }

    long dispatch_read(file* parent, void* buffer, u32 length, u64 offset) {
        const auto* capability = static_cast<const file_read_capability*>(
            parent->capability(file_capability::read));
        return capability != nullptr ? capability->read(parent, buffer, length, offset)
                                     : parent->read(buffer, length, offset);
    }

    long dispatch_write(file* parent, const void* buffer, u32 length, u64 offset) {
        const auto* capability = static_cast<const file_write_capability*>(
            parent->capability(file_capability::write));
        return capability != nullptr ? capability->write(parent, buffer, length, offset)
                                     : parent->write(buffer, length, offset);
    }

    long dispatch_ioctl(file* parent, u64 request, void* argp, usize size) {
        const auto* capability = static_cast<const file_ioctl_capability*>(
            parent->capability(file_capability::ioctl));
        return capability != nullptr ? capability->ioctl(parent, request, argp, size)
                                     : parent->ioctl(request, argp, size);
    }

    mm::phys_region* dispatch_mapping(const file* parent) {
        const auto* capability = static_cast<const file_mapping_capability*>(
            parent->capability(file_capability::mapping));
        return capability != nullptr ? capability->mapping(parent)
                                     : const_cast<file*>(parent)->memory_region();
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
        if (_parent)
            _parent->close(_flags);
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
        if (!_parent)
            return -EBADF;
        const bool current = offset == offset_current;
        const u64  actual  = current ? current_position() : offset;
        long       result  = dispatch_read(_parent.get(), buf, len, actual);
        if (result == -ENOSYS) {
            io_packet packet(mm::shptr<open_file>::borrowed(this), buf, len, actual,
                             io::direction::input);
            if (!packet)
                return -ENOMEM;
            if (!packet.request().park_device())
                return -EAGAIN;
            result = packet.submit();
            if (result == 0 && !packet.request().complete())
                return -EAGAIN;
            packet.request().acknowledge();
        }
        if (result > 0 && current)
            set_position(actual + static_cast<u64>(result));
        return result;
    }

    long open_file::seek(i64 offset, i32 whence) {
        if (!_parent)
            return -EBADF;
        const file_mode mode = _parent->mode();
        if (!mode.is_regular() && !mode.is_directory())
            return -ESPIPE;

        i64 base = 0;
        switch (whence) {
        case 0:
            break;
        case 1:
            if (current_position() > static_cast<u64>(~i64{0} >> 1))
                return -EOVERFLOW;
            base = static_cast<i64>(current_position());
            break;
        case 2: {
            const long size = this->size();
            if (size < 0)
                return size;
            base = static_cast<i64>(size);
            break;
        }
        default:
            return -EINVAL;
        }

        i64 target;
        if (__builtin_add_overflow(base, offset, &target))
            return -EOVERFLOW;
        if (target < 0)
            return -EINVAL;
        set_position(static_cast<u64>(target));
        return target;
    }

    long open_file::reserve_append(u32 length, u64* offset) {
        if (!_parent || offset == nullptr)
            return -EBADF;
        auto state = position_state();
        if (!state)
            return -ENOMEM;
        core::spinlock_guard guard(state->append_lock);
        const long           size = dispatch_size(_parent.get());
        if (size < 0)
            return size;
        u64 base = static_cast<u64>(size);
        if (state->append_initialized && state->append_position > base)
            base = state->append_position;
        if (static_cast<u64>(length) > ~u64{0} - base)
            return -EOVERFLOW;
        const u64 end = base + static_cast<u64>(length);
        if (end > static_cast<u64>(~i64{0} >> 1))
            return -EOVERFLOW;
        state->append_position    = end;
        state->append_initialized = true;
        *offset                   = base;
        return 0;
    }

    long open_file::write(const void* buf, u32 len, u64 offset) {
        if (!_parent)
            return -EBADF;
        const bool current = offset == offset_current;
        u64        actual  = current ? current_position() : offset;
        if ((_flags & static_cast<u32>(open_file_status_flags::append)) != 0) {
            const long reserved = reserve_append(len, &actual);
            if (reserved < 0)
                return reserved;
        }
        long result = dispatch_write(_parent.get(), buf, len, actual);
        if (result == -ENOSYS) {
            io_packet packet(mm::shptr<open_file>::borrowed(this), const_cast<void*>(buf),
                             len, actual, io::direction::output);
            if (!packet)
                return -ENOMEM;
            if (!packet.request().park_device())
                return -EAGAIN;
            result = packet.submit();
            if (result == 0 && !packet.request().complete())
                return -EAGAIN;
            packet.request().acknowledge();
        }
        if (result > 0 &&
            (current || (_flags & static_cast<u32>(open_file_status_flags::append)) != 0))
            set_position(actual + static_cast<u64>(result));
        return result;
    }
    long open_file::submit(io::request& request) {
        if (!_parent)
            return -EBADF;
        // Resolve the offset once, at submission, and write it back into
        // the request: every resume of a parked operation must land on the
        // same position the caller asked for, not on a moved _position.
        const bool current = request.offset() == offset_current;
        u64        actual  = current ? current_position() : request.offset();
        if ((_flags & static_cast<u32>(open_file_status_flags::append)) != 0 &&
            request.direction() == io::direction::output && !request.append_reserved()) {
            const u32 length =
                request.has_append_length() ? request.append_length() : request.length();
            const long reserved = reserve_append(length, &actual);
            if (reserved < 0)
                return reserved;
            request.append_reserved(true);
        }
        request.offset(actual);
        const long result = dispatch_submit(_parent.get(), request);
        if (result > 0 && (current || request.updates_position()))
            set_position(actual + static_cast<u64>(result));
        return result;
    }
    u32 open_file::poll(u32 events) const {
        if (!_parent)
            return static_cast<u32>(POLLNVAL);
        return dispatch_poll(_parent.get(), events, _flags);
    }
    long open_file::ioctl(u64 request, void* argp, usize size) {
        if (!_parent)
            return -EBADF;
        return dispatch_ioctl(_parent.get(), request, argp, size);
    }

    long open_file::acquire_controlling_terminal(i32 group) {
        if (!_parent)
            return -EBADF;
        return _parent->acquire_controlling_terminal(group);
    }

    long open_file::size() const {
        if (!_parent)
            return -EBADF;
        return dispatch_size(_parent.get());
    }
    long open_file::truncate(u64 length) {
        if (!_parent)
            return -EBADF;
        return _parent->truncate(length);
    }
    long open_file::chmod(file_permissions permissions) {
        if (!_parent)
            return -EBADF;
        return _parent->chmod(permissions);
    }
    long open_file::sync_mapping(mm::phys_region* mapping, u64 offset, u64 length,
                                 u32 flags) {
        if (!_parent)
            return -EBADF;
        return _parent->sync_mapping(mapping, offset, length, flags);
    }
    mm::phys_region* open_file::mapping() const {
        return _parent ? dispatch_mapping(_parent.get()) : nullptr;
    }

    file_mode open_file::mode() const {
        return !_parent ? _mode : _parent->mode();
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
        return !_parent ? 0 : _parent->mtime_sec();
    }
    u64 open_file::mtime_nsec() const {
        return !_parent ? 0 : _parent->mtime_nsec();
    }

} // namespace astia::fs
