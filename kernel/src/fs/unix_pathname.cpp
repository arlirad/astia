#include "unix_pathname.hpp"
#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>

namespace astia::fs {
    namespace {
        constexpr u32 max_unix_paths = 32;
        struct entry {
            bool used{};
            char path[path_max]{};
            mm::shptr<file> endpoint;
            mount* owner{};
            file_mode mode{file_type::socket, file_permissions(0777)};
            uid_t uid{};
            gid_t gid{};
        };
        entry entries[max_unix_paths];
        core::spinlock lock;

        bool equal(const char* a, const char* b) {
            if (!a || !b) return false;
            while (*a && *a == *b) { ++a; ++b; }
            return *a == *b;
        }

        bool parent_of(const char* directory, const char* path, u32& start);
    }

    long register_unix_path(const char* path, const mm::shptr<file>& endpoint,
                            uid_t uid, gid_t gid, char* canonical, u32 capacity) {
        if (!path || !endpoint || !canonical || !capacity) return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0) return rc;
        u32 length = 0;
        while (resolved[length]) ++length;
        if (length + 1 > capacity) return -ENAMETOOLONG;
        mount* owner = retain_mount_for_unix_path(resolved);
        if (!owner) return -ENOENT;
        long result = 0;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && equal(item.path, resolved)) {
                result = -EADDRINUSE;
                break;
            }
        entry* free = nullptr;
        if (!result) {
            for (auto& item : entries)
                if (!item.used) { free = &item; break; }
            if (!free)
                result = -ENFILE;
        }
        if (!result) {
            free->used = true;
            free->endpoint = endpoint;
            free->owner = owner;
            free->uid = uid;
            free->gid = gid;
            for (u32 i = 0; i <= length; ++i) {
                free->path[i] = resolved[i];
                canonical[i] = resolved[i];
            }
        }
        guard.unlock();
        if (result)
            release_mount_reference(owner);
        return result;
    }

    long lookup_unix_path(const char* path, mm::shptr<file>& endpoint) {
        if (!path) return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0) return rc;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && equal(item.path, resolved)) {
                endpoint = item.endpoint;
                return endpoint ? 0 : -ECONNREFUSED;
            }
        return -ENOENT;
    }

    void unregister_unix_path(const char* canonical, file* endpoint) {
        if (!canonical || !endpoint) return;
        mount* owner = nullptr;
        {
            core::spinlock_guard guard(lock);
            for (auto& item : entries)
                if (item.used && item.endpoint.get() == endpoint) {
                    owner = item.owner;
                    item = entry{};
                    break;
                }
        }
        if (owner) release_mount_reference(owner);
    }

    long remove_unix_path(const char* canonical, mount* owner, bool directory) {
        if (!canonical || !owner) return -EINVAL;
        mount* held = nullptr;
        {
            core::spinlock_guard guard(lock);
            for (auto& item : entries) {
                if (!item.used || item.owner != owner)
                    continue;
                if (equal(item.path, canonical)) {
                    if (directory) return -ENOTDIR;
                    held = item.owner;
                    item = entry{};
                    break;
                }
                if (directory) {
                    u32 start = 0;
                    if (parent_of(canonical, item.path, start)) return -ENOTEMPTY;
                }
            }
        }
        if (!held) return 0;
        release_mount_reference(held);
        return 1;
    }

    long rename_unix_path(const char* old_path, const char* new_path, mount* owner) {
        if (!old_path || !new_path || !owner) return -EINVAL;
        if (equal(old_path, new_path)) return 0;
        mount* replaced = nullptr;
        {
            core::spinlock_guard guard(lock);
            entry* source = nullptr;
            entry* target = nullptr;
            for (auto& item : entries) {
                if (!item.used) continue;
                if (item.owner == owner && equal(item.path, old_path)) source = &item;
                if (item.owner == owner && equal(item.path, new_path)) target = &item;
            }
            if (!source) return 0;
            if (target == source) return 0;
            if (target) {
                replaced = target->owner;
                target->endpoint = {};
                target->owner = nullptr;
                target->used = false;
            }
            for (u32 i = 0; i < path_max; ++i) {
                source->path[i] = new_path[i];
                if (!new_path[i]) break;
            }
        }
        if (replaced) release_mount_reference(replaced);
        return 1;
    }

    namespace {
        bool parent_of(const char* directory, const char* path, u32& start) {
            u32 n = 0;
            while (directory[n]) ++n;
            if (n == 1) {
                if (path[0] != '/') return false;
                start = 1;
            } else {
                for (u32 i = 0; i < n; ++i) if (path[i] != directory[i]) return false;
                if (path[n] != '/') return false;
                start = n + 1;
            }
            return path[start] != '\0';
        }
    }

    file_type unix_node_type(const char* canonical, mount* owner) {
        if (!canonical || !owner) return directory_unknown;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical))
                return directory_sock;
        return directory_unknown;
    }
    file_mode unix_node_mode(const char* canonical, mount* owner) {
        if (!canonical || !owner) return {};
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical))
                return item.mode;
        return {};
    }
    uid_t unix_node_uid(const char* canonical, mount* owner) {
        if (!canonical || !owner) return {};
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical)) return item.uid;
        return {};
    }
    gid_t unix_node_gid(const char* canonical, mount* owner) {
        if (!canonical || !owner) return {};
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical)) return item.gid;
        return {};
    }
    long unix_chmod_path(const char* canonical, mount* owner, file_permissions permissions) {
        if (!canonical || !owner) return -EINVAL;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical)) {
                item.mode = file_mode(file_type::socket, permissions);
                return 0;
            }
        return -ENOENT;
    }
    long unix_chown_path(const char* canonical, mount* owner, uid_t uid, gid_t gid) {
        if (!canonical || !owner) return -EINVAL;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && item.owner == owner && equal(item.path, canonical)) {
                item.uid = uid;
                item.gid = gid;
                return 0;
            }
        return -ENOENT;
    }
    long read_unix_directory(const char* directory, mount* owner, u64 index,
                             char* name, u32 capacity, file_type* type) {
        if (!directory || !owner || !name || !type || !capacity) return -EINVAL;
        core::spinlock_guard guard(lock);
        u64 ordinal = 0;
        for (auto& item : entries) {
            if (!item.used || item.owner != owner) continue;
            u32 start = 0;
            if (!parent_of(directory, item.path, start)) continue;
            u32 end = start;
            while (item.path[end] && item.path[end] != '/') ++end;
            if (item.path[end] != '\0') continue;
            // A pathname record is one directory entry; duplicate prefixes
            // cannot occur because bind names are unique.
            if (ordinal++ != index) continue;
            if (end - start + 1 > capacity) return -ENAMETOOLONG;
            for (u32 i = start; i < end; ++i) name[i - start] = item.path[i];
            name[end - start] = '\0';
            *type = directory_sock;
            return 1;
        }
        return 0;
    }
}
