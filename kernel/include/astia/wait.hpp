#pragma once

#include <astia/api/wait.h>
#include <astia/common.hpp>

namespace astia {
    inline constexpr i32 wait_signal_mask      = ASTIA_WAIT_SIGNAL_MASK;
    inline constexpr i32 wait_core_bit         = ASTIA_WAIT_CORE_BIT;
    inline constexpr i32 wait_exit_mask        = ASTIA_WAIT_EXIT_MASK;
    inline constexpr i32 wait_stopped_marker   = ASTIA_WAIT_STOPPED_MARKER;
    inline constexpr i32 wait_continued_marker = ASTIA_WAIT_CONTINUED_MARKER;
    inline constexpr i32 wait_signal_exit_base = ASTIA_WAIT_SIGNAL_EXIT_BASE;

    constexpr i32 wait_status_from_exit(i32 status) {
        return (status & wait_exit_mask) << 8;
    }

    constexpr i32 wait_status_from_signal(i32 signal) {
        return signal & wait_signal_mask;
    }

    constexpr i32 wait_status_from_stop(i32 signal) {
        return (signal << 8) | wait_stopped_marker;
    }

    constexpr i32 wait_signal_exit_status(i32 signal) {
        return wait_signal_exit_base + signal;
    }
} // namespace astia
