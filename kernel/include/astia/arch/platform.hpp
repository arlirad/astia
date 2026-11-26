#pragma once

#include <astia/common.hpp>

namespace astia::arch {
    /// Canonical machine name of the selected architecture port, as reported
    /// by uname(2). Returns nullptr if the port has no machine identity.
    const char* machine_name();

    /// Publish architecture-specific scheduler state after proc init.
    bool init_scheduler();

    namespace platform {
        inline constexpr u32 max_cpu_count = 256;

        constexpr u32 clamp_cpu_count(u64 count) {
            return count == 0              ? 1
                   : count > max_cpu_count ? max_cpu_count
                                           : static_cast<u32>(count);
        }

        u32 online_cpu_count();

        /// Terminate the emulator through the port's debug-exit device.
        /// Ports without one may leave this as a no-op.
        void debug_exit(u32 status);
    } // namespace platform
} // namespace astia::arch
