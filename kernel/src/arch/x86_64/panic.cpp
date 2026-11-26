#include <astia/dbg/panic_report.hpp>

namespace astia::arch {
    u64 panic_address_space_root() {
        u64 value = 0;

        asm volatile("mov %%cr3, %0" : "=r"(value));

        return value;
    }

    void capture_panic_registers(dbg::panic_registers& out) {
        out           = {};
        out.arch      = "x86_64";
        out.x86       = true;
        out.gpr_count = 16;

        asm volatile("movq %%rax, %0" : "=m"(out.gpr[0]));
        asm volatile("movq %%rbx, %0" : "=m"(out.gpr[1]));
        asm volatile("movq %%rcx, %0" : "=m"(out.gpr[2]));
        asm volatile("movq %%rdx, %0" : "=m"(out.gpr[3]));
        asm volatile("movq %%rsi, %0" : "=m"(out.gpr[4]));
        asm volatile("movq %%rdi, %0" : "=m"(out.gpr[5]));
        asm volatile("movq %%rbp, %0" : "=m"(out.gpr[6]));
        asm volatile("movq %%rsp, %0" : "=m"(out.gpr[7]));
        asm volatile("movq %%r8, %0" : "=m"(out.gpr[8]));
        asm volatile("movq %%r9, %0" : "=m"(out.gpr[9]));
        asm volatile("movq %%r10, %0" : "=m"(out.gpr[10]));
        asm volatile("movq %%r11, %0" : "=m"(out.gpr[11]));
        asm volatile("movq %%r12, %0" : "=m"(out.gpr[12]));
        asm volatile("movq %%r13, %0" : "=m"(out.gpr[13]));
        asm volatile("movq %%r14, %0" : "=m"(out.gpr[14]));
        asm volatile("movq %%r15, %0" : "=m"(out.gpr[15]));

        u64 flags = 0;

        asm volatile("pushfq; popq %0" : "=r"(flags));

        out.rflags = flags;
        out.rsp    = out.gpr[7];
        out.rbp    = out.gpr[6];
        out.rip    = reinterpret_cast<u64>(__builtin_return_address(0));
        out.cr3    = panic_address_space_root();

        asm volatile("mov %%cr0, %0" : "=r"(out.cr0));
        asm volatile("mov %%cr2, %0" : "=r"(out.cr2));
        asm volatile("mov %%cr4, %0" : "=r"(out.cr4));
    }
} // namespace astia::arch
