#pragma once

#include <astia/common.hpp>

// Timer-source contract shared by the architecture ports and the generic
// clock callers.  It is deliberately free of <concepts>/<type_traits> so the
// freestanding AArch64 port can include it without a hosted C++ standard
// library; the interrupt-frame concept remains in <astia/arch/interrupts.hpp>.
namespace astia::arch {
    /// Ticks counted from the timer interrupt since init_interrupts();
    /// 0 until the first tick. Grows monotonically for the life of the
    /// kernel — the time source for idle loops and timeouts. A port with a
    /// periodic tick service may account whole missed periods so wall time
    /// keeps advancing when a handler is delayed.
    u64 timer_ticks();

    /// Frequency of timer_ticks() in hertz — the period one tick covers,
    /// and therefore the resolution any clock read can offer. 0 means "no
    /// timer source yet" (the aarch64 port before its timer lands): a
    /// caller converting ticks to seconds must refuse rather than divide.
    u32 timer_hz();

    /// Which timer source drives timer_ticks(): the per-CPU LAPIC timer
    /// (SMP-scalable — one LAPIC per core) when bring-up calibrated it,
    /// the boot PIT otherwise, or the AArch64 EL1 physical timer's periodic
    /// PPI tick service. Diagnostics today; per-CPU once SMP lands.
    enum class timer_source : u8 { pit, lapic, generic };

    timer_source current_timer_source();

    /// Callback invoked from the timer interrupt with interrupts masked.
    /// The scheduler owns the policy; the architecture only delivers the
    /// periodic event.
    using timer_hook = void (*)();
    bool register_timer_hook(timer_hook hook);
} // namespace astia::arch
