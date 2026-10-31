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
        };
        entry entries[max_unix_paths];
        core::spinlock lock;

        bool equal(const char* a, const char* b) {
            if (!a || !b) return false;
            while (*a && *a == *b) { ++a; ++b; }
            return *a == *b;
        }
    }

    long register_unix_path(const char* path, const mm::shptr<file>& endpoint,
                            char* canonical, u32 capacity) {
        if (!path || !endpoint || !canonical || !capacity) return -EINVAL;
        char resolved[path_max]{};
        long rc = resolve_path(path, resolved, sizeof resolved, false);
        if (rc < 0) return rc;
        u32 length = 0;
        while (resolved[length]) ++length;
        if (length + 1 > capacity) return -ENAMETOOLONG;
        mount* owner = retain_mount_for_unix_path(resolved);
        if (!owner) return -ENOENT;
        core::spinlock_guard guard(lock);
        for (auto& item : entries)
            if (item.used && equal(item.path, resolved)) {
                release_mount_reference(owner);
                return -EADDRINUSE;
            }
        entry* free = nullptr;
        for (auto& item : entries)
            if (!item.used) { free = &item; break; }
        if (!free) {
            release_mount_reference(owner);
            return -ENFILE;
        }
        free->used = true;
        free->endpoint = endpoint;
        free->owner = owner;
        for (u32 i = 0; i <= length; ++i) {
            free->path[i] = resolved[i];
            canonical[i] = resolved[i];
        }
        return 0;
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
                if (item.used && item.endpoint.get() == endpoint &&
                    equal(item.path, canonical)) {
                    owner = item.owner;
                    item = entry{};
                    break;
                }
        }
        if (owner) release_mount_reference(owner);
    }
}
