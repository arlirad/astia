#pragma once

#include <astia/arch/timer.hpp>
#include <astia/common.hpp>

#include <concepts>

namespace astia::arch {
    struct interrupt_frame;

    /// Stack pointer of the interrupted ring-3 context (fault path).
    u64 interrupt_stack_pointer(const interrupt_frame* frame);

    /// Redirect a faulting ring-3 context at a signal handler.
    void interrupt_enter_signal(interrupt_frame* frame, u64 handler, u64 stack, u64 signo,
                                u64 info, u64 context);

    template <typename T>
    concept interrupt_frame_image = requires(T* frame) {
        { interrupt_stack_pointer(frame) } -> std::convertible_to<u64>;
        { interrupt_enter_signal(frame, u64{}, u64{}, u64{}, u64{}, u64{}) };
    };

    static_assert(interrupt_frame_image<interrupt_frame>);

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

    /// Number of int3 breakpoints dispatched since init_interrupts() — a
    /// debugger bumps it, and so does the boot selftest, which uses a
    /// software int3 to prove the IDT dispatch path works before opening
    /// the hardware IRQ gate.
    u64 breakpoint_hits();

    /// User page-fault resolver, registered by the process layer: the
    /// #PF path offers it every ring-3 fault before declaring it fatal.
    /// True = the fault was repaired (a copy-on-write page was copied
    /// out or regained its write bit) and the faulting instruction
    /// re-executes; false = genuine violation, stays fatal.
    using user_fault_handler = bool (*)(u64 addr, bool write);
    bool register_user_fault_handler(user_fault_handler handler);
    using user_fault_signal_handler = bool (*)(interrupt_frame* frame, u64 addr,
                                               u64 error);
    bool register_user_fault_signal_handler(user_fault_signal_handler handler);
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

    struct interrupt_vector {
        u32 vector = 0;
        u32 owner  = 0;

        bool valid() const {
            return vector != 0 && owner != 0;
        }
    };

    using interrupt_vector_handler = void (*)(void* context);

    bool register_interrupt_vector(interrupt_vector_handler handler, void* context,
                                   interrupt_vector& route);
    bool unregister_interrupt_vector(interrupt_vector route);
    bool mask_interrupt_vector(interrupt_vector route);
    bool unmask_interrupt_vector(interrupt_vector route);

    struct msi_message {
        u32 address_low  = 0;
        u32 address_high = 0;
        u32 data         = 0;
    };

    bool interrupt_vector_message(interrupt_vector route, msi_message& message);
} // namespace astia::arch
