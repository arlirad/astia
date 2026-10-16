#include "pit.hpp"

#include "io.hpp"

// PIT channel 0 only (IRQ0). Mode 3 runs a free-running square wave —
// the tick interrupt; mode 0 counts one span down to 0 and stops — the
// calibration yardstick, pollable without any interrupt delivery.

namespace astia::arch::x86_64 {

    namespace {

        constexpr u16 ch0_data = 0x40;
        constexpr u16 mode_cmd = 0x43;

        /// Write a 16-bit divisor to channel 0 (lo/hi access mode set by
        /// the preceding mode_cmd).
        void write_divisor(u16 divisor) {
            outb(ch0_data, static_cast<u8>(divisor & 0xff));
            outb(ch0_data, static_cast<u8>(divisor >> 8));
        }

    } // namespace

    void pit::start(u32 hz) {
        outb(mode_cmd, 0x36); // ch0, lo/hi, mode 3 (square wave)
        write_divisor(static_cast<u16>(input_hz / hz));
    }

    void pit::start_one_shot(u32 counts) {
        outb(mode_cmd, 0x30); // ch0, lo/hi, mode 0 (one-shot)
        write_divisor(static_cast<u16>(counts));
    }

    u32 pit::read_count() {
        outb(mode_cmd, 0x00); // latch ch0
        const u8 lo = inb(ch0_data);
        const u8 hi = inb(ch0_data);
        return static_cast<u32>(lo | (hi << 8));
    }

} // namespace astia::arch::x86_64
