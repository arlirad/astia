#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// CPU primitives, provided by the selected arch
    /// (src/arch/<arch>/cpu.cpp): halt, interrupt gating, and later the
    /// identity/per-CPU state that SMP bringup needs.
    ///
    /// The static methods act on the CPU the code is currently running on —
    /// the boot CPU until SMP gives every logical CPU an identity. As SMP
    /// lands this class also becomes the representation of a CPU (identity,
    /// per-CPU data) while the primitives stay as they are.
    class cpu {
        public:
        /// Wait for the next interrupt (`hlt` / `wfi`). With the gate closed
        /// this halts until an NMI — the "park forever" building block for
        /// idle loops and unrecoverable-error paths.
        static auto halt() -> void;

        /// Open the maskable-interrupt gate (`sti` / DAIF.I clear): from
        /// here on, armed interrupt sources may dispatch at all.
        static auto interrupts() -> void;

        /// Close the maskable-interrupt gate (`cli` / DAIF.I set). Closing it
        /// is what makes a lock's critical section safe against an interrupt
        /// handler that may take the same lock (see core::spinlock, which
        /// deliberately leaves gating to callers).
        static auto nointerrupts() -> void;

        /// Whether the maskable-interrupt gate is currently open
        /// (RFLAGS.IF / DAIF.I): the state to save before nointerrupts() and
        /// restore with interrupts().
        static auto check_interrupts() -> bool;

        /// Set the ring-0 stack used when entering from usermode.  Stub
        /// architectures may leave this as a no-op until their exception
        /// level transition is implemented.
        static auto set_kernel_stack(u64 stack_top) -> void;
    };

    /// Scoped mask of the maskable-interrupt gate: nointerrupts() on
    /// construction, the prior state restored on destruction. This is the
    /// interrupt half of a critical section that spans core::spinlock — mask
    /// *before* taking the lock, so an interrupt handler on this CPU cannot
    /// deadlock on a lock this CPU already holds:
    ///
    ///     astia::arch::interrupt_guard gate;      // interrupts off
    ///     core::spinlock_guard lock_guard(lock);  // then take the lock
    ///     ...
    ///
    /// Non-copyable and non-movable: a copy or move would restore the gate
    /// in the wrong frame's destructor.
    class interrupt_guard {
        public:
        interrupt_guard() : open_(cpu::check_interrupts()) {
            cpu::nointerrupts();
        }
        ~interrupt_guard() {
            if (open_) {
                cpu::interrupts();
            }
        }
        interrupt_guard(const interrupt_guard&)                    = delete;
        auto operator=(const interrupt_guard&) -> interrupt_guard& = delete;

        private:
        bool open_; // was the gate open when we masked it?
    };

} // namespace astia::arch
