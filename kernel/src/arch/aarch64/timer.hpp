#pragma once

#include <astia/common.hpp>

// Bounded EL1 physical timer contract for the QEMU virt boot stage:
// PPI 30, Non-secure Group 1, priority 0xa0, level-sensitive, one-shot TVAL
// below INT32_MAX.  The timer is disabled and masked before EOIR/DIR so the
// level remains deasserted; no periodic or generic timer abstraction exists.
namespace astia::arch::aarch64::timer {

    inline constexpr u32 ppi_id               = 30;
    inline constexpr u32 ctl_enable           = 1u << 0;
    inline constexpr u32 ctl_imask            = 1u << 1;
    inline constexpr u32 ctl_istatus          = 1u << 2;
    inline constexpr u32 ctl_disabled_masked  = ctl_imask;
    inline constexpr u32 one_shot_interval_ms = 10;
    inline constexpr u32 tval_max_positive    = 0x7fffffffu;

    enum class error : u8 {
        none,
        frequency_zero,
        interval_unrepresentable,
    };

    const char* describe(error value);

    [[nodiscard]] constexpr bool one_shot_ticks(u32 frequency_hz, u32 interval_ms,
                                                u32& ticks) {
        if (frequency_hz == 0 || interval_ms == 0) {
            return false;
        }
        const u64 frequency = frequency_hz;
        const u64 interval  = interval_ms;
        if (frequency > ~u64{0} / interval) {
            return false;
        }
        const u64 scaled = frequency * interval;
        if (scaled > ~u64{0} - 999) {
            return false;
        }
        const u64 rounded = (scaled + 999) / 1000;
        if (rounded == 0 || rounded > tval_max_positive) {
            return false;
        }
        ticks = static_cast<u32>(rounded);
        return true;
    }

    struct arm_result {
        u32 frequency_hz = 0;
        u32 ticks        = 0;
        u32 control      = 0;
    };

    error arm_one_shot(arm_result& result);
    void  disable_and_clear();
    u32   read_control();

} // namespace astia::arch::aarch64::timer
