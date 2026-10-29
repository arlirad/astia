#pragma once

#include "mount.hpp"

namespace astia::fs {

    /// A registered filesystem type is a factory for mount instances.
    /// Type objects are kernel-owned and static; mount() returns the
    /// mounted instance, not a second filesystem type.
    class filesystem {
        public:
        virtual const char*       name() const                    = 0;
        virtual astia::fs::mount* mount(const char* source) const = 0;
    };

    long register_type(const filesystem* type);
    long mount_type(const char* type, const char* target, const char* source = nullptr);
    /// Remove a non-root mount after all handles and child mounts are gone.
    long unmount_type(const char* target);

} // namespace astia::fs
