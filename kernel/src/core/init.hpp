#pragma once

#include <astia/common.hpp>

namespace astia::core {
    /// Kernel initialization sequence
    void init_early();
    void init_memory();
    void init_devices();
    void init_subsystems();
    void init_late();
} // namespace astia::core
