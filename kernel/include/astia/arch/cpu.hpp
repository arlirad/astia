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
        static void halt();

        /// Open the maskable-interrupt gate (`sti` / DAIF.I clear): from
        /// here on, armed interrupt sources may dispatch at all.
        static void interrupts();

        /// Close the maskable-interrupt gate (`cli` / DAIF.I set). Closing it
        /// is what makes a lock's critical section safe against an interrupt
        /// handler that may take the same lock (see core::spinlock, which
        /// deliberately leaves gating to callers).
        static void nointerrupts();

        /// Whether the maskable-interrupt gate is currently open
        /// (RFLAGS.IF / DAIF.I): the state to save before nointerrupts() and
        /// restore with interrupts().
        static bool check_interrupts();

        /// Set the ring-0 stack used when entering from usermode.  Stub
        /// architectures may leave this as a no-op until their exception
        /// level transition is implemented.
        static void set_kernel_stack(u64 stack_top);
        static void request_reschedule();
        /// Ask this CPU to schedule at the next safe return boundary.
        static void request_local_reschedule();
        /// Consume this CPU's deferred scheduling request.
        static bool consume_reschedule();

        /// Logical CPU index assigned during SMP bring-up.  The value is
        /// CPU-local and remains stable for the lifetime of the CPU.
        static u32 current_index();
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
        interrupt_guard(const interrupt_guard&)            = delete;
        interrupt_guard& operator=(const interrupt_guard&) = delete;

        private:
        bool open_; // was the gate open when we masked it?
    };

} // namespace astia::arch
