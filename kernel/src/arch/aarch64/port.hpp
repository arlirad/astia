#pragma once

// Hooks the AArch64 boot stage calls from its own interrupt paths.  They live
// in the arch port so the timer IRQ entry can tick the generic timer contract
// without exposing the port's internals.
namespace astia::arch::aarch64 {
    /// Account for one PPI 30 timer interrupt and deliver the registered
    /// arch timer hook.  Called from the timer handler before EOIR/DIR.
    void notify_timer_tick();
} // namespace astia::arch::aarch64
