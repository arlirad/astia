#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Arm maskable-interrupt delivery on the boot CPU, provided by the
    /// selected arch (src/arch/<arch>/interrupts.cpp): install the arch's
    /// interrupt table with handlers for CPU exceptions and device IRQs, and
    /// start the periodic timer that drives the tick interrupt. On x86_64 the
    /// tick comes from the local APIC timer, calibrated against the PIT; the
    /// PIT remains as the fallback when the LAPIC is unusable.
    ///
    /// Interrupts stay *masked* on return — arming and opening the gate are
    /// deliberately separate (see cpu::interrupts()), so the caller decides
    /// when dispatch may begin.
    bool init_interrupts();

    /// Ticks counted from the timer interrupt since init_interrupts();
    /// 0 until the first tick. Grows monotonically for the life of the
    /// kernel — the time source for idle loops and timeouts.
    u64 timer_ticks();

    /// Frequency of timer_ticks() in hertz — the period one tick covers,
    /// and therefore the resolution any clock read can offer. 0 means "no
    /// timer source yet" (the aarch64 port before its timer lands): a
    /// caller converting ticks to seconds must refuse rather than divide.
    u32 timer_hz();

    /// Which timer source drives timer_ticks(): the per-CPU LAPIC timer
    /// (SMP-scalable — one LAPIC per core) when bring-up calibrated it,
    /// the boot PIT otherwise. Diagnostics today; per-CPU once SMP lands.
    enum class timer_source : u8 { pit, lapic };
    timer_source current_timer_source();

    /// Number of int3 breakpoints dispatched since init_interrupts() — a
    /// debugger bumps it, and so does the boot selftest, which uses a
    /// software int3 to prove the IDT dispatch path works before opening
    /// the hardware IRQ gate.
    u64 breakpoint_hits();

    /// Callback invoked from the timer interrupt with interrupts masked.
    /// The scheduler owns the policy; the architecture only delivers the
    /// periodic event.
    using timer_hook = void (*)();
    bool register_timer_hook(timer_hook hook);

    /// User page-fault resolver, registered by the process layer: the
    /// #PF path offers it every ring-3 fault before declaring it fatal.
    /// True = the fault was repaired (a copy-on-write page was copied
    /// out or regained its write bit) and the faulting instruction
    /// re-executes; false = genuine violation, stays fatal.
    using user_fault_handler = bool (*)(u64 addr, bool write);
    bool register_user_fault_handler(user_fault_handler handler);
    void load_interrupts();

    /// Handler for one maskable device IRQ line, provided by the driver
    /// that owns the device.  Runs with the interrupt gate closed (an
    /// interrupt gate clears IF on entry), before the line is EOI'd —
    /// so the handler may complete work (post a ring completion, wake a
    /// waiter) and the next edge of its line is only acknowledged once
    /// the device has been serviced.
    using irq_handler = void (*)();

    /// Claim a maskable IRQ line for a driver.  On x86_64 `irq` is the
    /// PIC line number (0..15), dispatched as vector pic::irq_base + irq;
    /// the timer lines (IRQ0 / the LAPIC timer vector) are reserved.
    /// One handler per line — a second claim of a taken line fails, so
    /// shared-line schemes must be built on top, not underneath.
    /// Registration does not unmask anything: the driver unmasks its
    /// own PIC line when it is ready to service (pic::unmask).
    bool register_irq_handler(u8 irq, irq_handler handler);

} // namespace astia::arch
