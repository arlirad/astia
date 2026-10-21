#pragma once

#include <astia/common.hpp>

// Local APIC (xAPIC, MMIO): per-CPU timer, the SMP-ready tick source — the
// PIT is one global device, a LAPIC exists on every core. Its frequency is
// measured against the PIT at bring-up rather than assumed, then it runs
// periodically at the requested rate.

namespace astia::arch::x86_64 {

    class lapic {
        public:
        static constexpr u32 timer_vector = 48; // past the PIC's 32..47
        static constexpr u32 reschedule_vector = 49;

        /// Bring the LAPIC up as the tick source at `hz`: map the MMIO
        /// window, software-enable the APIC, measure the timer against a
        /// PIT one-shot, and program it periodic. False when the LAPIC is
        /// absent/disabled or in x2APIC mode (not driven yet) — the caller
        /// falls back to the PIT.
        static bool start(u32 hz);

        /// LAPIC end-of-interrupt, for vectors the LAPIC delivered. No-op
        /// when the LAPIC never came up.
        static void eoi();
        static bool enable();
        static u32 id();
        static bool send_ipi(u32 apic_id, u8 vector);
        static bool init_sipi(u32 apic_id, u8 vector);
        static bool init_sipi_all(u8 vector);
    };

} // namespace astia::arch::x86_64
