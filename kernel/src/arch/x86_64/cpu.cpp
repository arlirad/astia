#include <astia/arch/cpu.hpp>
#include <astia/arch/x86_64/gdt.hpp>

namespace astia::arch {

    auto cpu::halt() -> void {
        asm volatile("hlt");
    }

    auto cpu::interrupts() -> void {
        // "memory" so surrounding loads/stores cannot be hoisted across the
        // point interrupts become visible again.
        asm volatile("sti" ::: "memory");
    }

    auto cpu::nointerrupts() -> void {
        asm volatile("cli" ::: "memory");
    }

    auto cpu::check_interrupts() -> bool {
        u64 flags;
        asm volatile("pushfq; popq %0" : "=r"(flags) : : "memory");
        return (flags & (1ull << 9)) != 0; // RFLAGS.IF
    }

    auto cpu::set_kernel_stack(u64 stack_top) -> void {
        x86_64::set_kernel_stack(stack_top);
    }

} // namespace astia::arch
