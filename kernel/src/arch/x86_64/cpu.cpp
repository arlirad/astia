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

    void cpu::interrupt_enter() {
        asm volatile("incl %%gs:24" ::: "memory");
    }

    void cpu::interrupt_exit() {
        u32 depth;
        asm volatile("decl %%gs:24; movl %%gs:24, %0" : "=r"(depth)::"memory");
        if (depth == 0 && x86_64::smp::consume_reschedule(current_index())) {
            asm volatile("movl $1, %%gs:28" ::: "memory");
        }
    }

    bool cpu::in_interrupt() {
        u32 depth;
        asm volatile("movl %%gs:24, %0" : "=r"(depth)::"memory");
        return depth != 0;
    }

    bool cpu::consume_should_yield() {
        u32 value = 0;
        asm volatile("xchgl %0, %%gs:28" : "+r"(value)::"memory");
        return value != 0;
    }

    void cpu::request_reschedule() {
        x86_64::smp::request_reschedule();
    }

    void cpu::request_local_reschedule() {
        x86_64::smp::request_local_reschedule();
    }

    bool cpu::consume_reschedule() {
        return x86_64::smp::consume_reschedule(current_index());
    }

    u32 cpu::current_index() {
        u32 value;
        asm volatile("movl %%gs:0, %0" : "=r"(value));
        return value;
    }

} // namespace astia::arch
