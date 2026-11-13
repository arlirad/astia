#pragma once

#include "mount.hpp"

namespace astia::fs {

    inline constexpr u32 invalid_filesystem_slot = ~u32{0};

    struct filesystem_registration {
        u32 slot       = invalid_filesystem_slot;
        u32 generation = 0;

        constexpr bool valid() const {
            return slot != invalid_filesystem_slot && generation != 0;
        }
    };

    /// A registered filesystem type is a factory for mount instances.
    /// Type objects are kernel-owned and static; mount() returns the
    /// mounted instance, not a second filesystem type.
    class filesystem {
        public:
        virtual const char*       name() const                    = 0;
        virtual astia::fs::mount* mount(const char* source) const = 0;
    };

    long register_type(const filesystem* type);
    long register_type(const filesystem* type, filesystem_registration& registration);
    long unregister_type(filesystem_registration& registration);
    long unregister_type(const filesystem* type);
    long mount_type(const char* type, const char* target, const char* source = nullptr);
    /// Remove a non-root mount after all handles and child mounts are gone.
    long unmount_type(const char* target);

} // namespace astia::fs
