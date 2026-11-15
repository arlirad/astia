#pragma once

// Cross-module forward declarations owned by the boot module. Include this
// (not <astia/boot.hpp>) from headers that only need the names, so the full
// definitions stay out of unrelated translation units.

namespace astia::boot {
    struct module_info;
    struct boot_info;
} // namespace astia::boot
