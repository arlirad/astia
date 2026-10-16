#pragma once

#include <astia/common.hpp>

// The two cascaded 8259 PICs. Remapped so IRQ0..15 arrive as vectors
// [irq_base, irq_base+16) — the BIOS-era values collide with CPU
// exceptions 0..15. Remap leaves every line masked; each driver unmasks
// its own line.

namespace astia::arch::x86_64 {

    class pic {
        public:
        static constexpr u16 irq_base = 32;

        /// Remap master/slave to [irq_base, irq_base+16), 8086 mode, and
        /// mask all 16 lines.
        static auto remap() -> void;

        /// Unmask a single IRQ line (0..15); everything else stays masked.
        static auto unmask(u8 irq) -> void;

        /// Non-specific EOI for a vector in [irq_base, irq_base+16); slave
        /// vectors (IRQ8..15) need both EOIs because of the IRQ2 cascade.
        static auto eoi(u64 vector) -> void;
    };

} // namespace astia::arch::x86_64
