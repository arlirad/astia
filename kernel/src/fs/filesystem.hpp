#pragma once

#include "mount.hpp"

namespace astia::fs {

    /// A registered filesystem type is a factory for mount instances.
    /// Type objects are kernel-owned and static; mount() returns the
    /// mounted instance, not a second filesystem type.
    class filesystem {
        public:
        virtual auto name() const -> const char*               = 0;
        virtual auto mount(const char* source) const -> mount* = 0;
    };

    auto register_type(const filesystem* type) -> long;
    auto mount_type(const char* type, const char* target, const char* source = nullptr)
        -> long;

} // namespace astia::fs
