#pragma once

#include <astia/common.hpp>

// PIT channel 0: the boot time source (mode 3 square wave -> IRQ0 at a
// programmed rate) and the reference clock the LAPIC timer is calibrated
// against (mode 0 one-shot, polled — calibration needs no interrupts).

namespace astia::arch::x86_64 {
    class pit {
        public:
        static constexpr u32 input_hz = 1193182; // PIT input frequency

        /// Channel 0, mode 3 (square wave): IRQ0 fires every 1/hz seconds.
        static void start(u32 hz);

        /// Channel 0, mode 0 (one-shot): counts `counts` input ticks down
        /// to 0 and stops; the countdown is observable with read_count().
        static void start_one_shot(u32 counts);

        /// Latched read of the channel-0 counter (freeze, then read back):
        /// counts down towards 0 in one-shot mode.
        static u32 read_count();
    };
} // namespace astia::arch::x86_64
