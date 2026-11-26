#pragma once

#include <astia/common.hpp>

// Kernel symbol lookup for loadable modules.

namespace astia::mod {
    /// Resolve a global, default-visible symbol retained in the linked kernel
    /// image. Returns 0 if unknown or if the table is unavailable.
    u64 resolve(const char* name);
} // namespace astia::mod
