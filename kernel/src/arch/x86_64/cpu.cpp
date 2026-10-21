#include "gdt.hpp"
#include "smp.hpp"
#include <astia/arch/cpu.hpp>

namespace astia::arch {

    void cpu::halt() {
        asm volatile("hlt");
    }

    void cpu::interrupts() {
        // "memory" so surrounding loads/stores cannot be hoisted across the
        // point interrupts become visible again.
        asm volatile("sti" ::: "memory");
    }

    void cpu::nointerrupts() {
        asm volatile("cli" ::: "memory");
    }

    bool cpu::check_interrupts() {
        u64 flags;
        asm volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
        return (flags & (1ull << 9)) != 0; // RFLAGS.IF
    }

    void cpu::set_kernel_stack(u64 stack_top) {
        x86_64::set_kernel_stack(stack_top);
    }

    void cpu::request_reschedule() {
        x86_64::smp::request_reschedule();
    }

    u32 cpu::current_index() {
        u32 value;
        asm volatile("movl %%gs:0, %0" : "=r"(value));
        return value;
    }

} // namespace astia::arch
