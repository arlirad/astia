#include "module_filesystem.hpp"

#include <astia/api/errno.h>
#include <astia/core/spinlock.hpp>

namespace astia::fs {

    namespace {

        inline constexpr u32 max_module_filesystems = 16;

        struct entry {
            bool used = false;
            u32 generation = 0;
            char name[module_filesystem_name_max]{};
        };

        entry entries[max_module_filesystems];
        core::spinlock registry_lock;
        usize count = 0;
        u32 next_generation = 1;

        bool equal(const char* lhs, const char* rhs) {
            if (lhs == nullptr || rhs == nullptr)
                return false;
            while (*lhs != '\0' && *lhs == *rhs) {
                ++lhs;
                ++rhs;
            }
            return *lhs == *rhs;
        }

        bool copy_name(char* destination, const char* source) {
            if (source == nullptr || source[0] == '\0')
                return false;
            u32 i = 0;
            for (; source[i] != '\0'; ++i)
                if (i + 1 >= module_filesystem_name_max)
                    return false;
            for (u32 j = 0; j < i; ++j)
                destination[j] = source[j];
            destination[i] = '\0';
            return true;
        }

    } // namespace

    long register_module_filesystem(const char* name,
                                    module_filesystem_registration& registration) {
        if (name == nullptr || name[0] == '\0' || registration.valid())
            return -EINVAL;

        core::spinlock_guard guard(registry_lock);
        for (const entry& item : entries)
            if (item.used && equal(item.name, name))
                return -EEXIST;

        for (u32 i = 0; i < max_module_filesystems; ++i) {
            entry& item = entries[i];
            if (item.used || !copy_name(item.name, name))
                continue;
            item.used       = true;
            item.generation = next_generation++;
            if (item.generation == 0)
                item.generation = next_generation++;
            registration = {i, item.generation};
            ++count;
            return 0;
        }
        return -ENFILE;
    }

    long unregister_module_filesystem(module_filesystem_registration& registration) {
        if (!registration.valid() || registration.slot >= max_module_filesystems)
            return -EINVAL;

        core::spinlock_guard guard(registry_lock);
        entry& item = entries[registration.slot];
        if (!item.used || item.generation != registration.generation)
            return -ENOENT;
        item = entry{};
        --count;
        registration = module_filesystem_registration{};
        return 0;
    }

    usize module_filesystem_count() {
        core::spinlock_guard guard(registry_lock);
        return count;
    }

} // namespace astia::fs
