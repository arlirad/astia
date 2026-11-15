#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Publish architecture-specific scheduler state after proc init.
    bool init_scheduler();

    namespace platform {

        inline constexpr u32 max_cpu_count = 256;

        constexpr u32 clamp_cpu_count(u64 count) {
            return count == 0 ? 1
                              : count > max_cpu_count ? max_cpu_count
                                                      : static_cast<u32>(count);
        }

        u32 online_cpu_count();

    } // namespace platform

} // namespace astia::arch
