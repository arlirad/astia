#include "gdt.hpp"

#include "heap.hpp"
#include "io.hpp"

namespace astia::arch::x86_64 {

    extern "C" {
    u64  astia_syscall_stack_top = 0;
    u64  astia_syscall_saved_r12 = 0;
    void astia_syscall_entry();
    }

    namespace {
        struct [[gnu::packed]] gdtr {
            u16 limit;
            u64 base;
        };

        struct [[gnu::packed]] tss {
            u32 reserved0;
            u64 rsp0;
            u64 rsp1;
            u64 rsp2;
            u64 reserved1;
            u64 ist1;
            u64 ist2;
            u64 ist3;
            u64 ist4;
            u64 ist5;
            u64 ist6;
            u64 ist7;
            u64 reserved2;
            u16 reserved3;
            u16 iomap_base;
        };

        static_assert(sizeof(tss) == 104);

        alignas(8) u64 gdt[7];
        alignas(16) tss task_state;
        gdtr  descriptor;
        void* kernel_stack;

        extern "C" auto astia_load_gdt_tss(const gdtr*, u16 selector) -> void;

        auto tss_descriptor(u64 base, u32 limit) -> void {
            const u64 access = 0x89ull << 40; // present, ring 0, available TSS
            const u64 low    = (limit & 0xFFFFu) | ((base & 0xFFFFFFu) << 16) | access |
                               (static_cast<u64>((limit >> 16) & 0xFu) << 48) |
                               ((base >> 24) & 0xFFull) << 56;
            gdt[5]           = low;
            gdt[6]           = base >> 32;
        }

        // x86-64 userspace has SSE2 baked into its ABI — compilers emit
        // pxor/movups for plain struct zeroing — so ring 3 needs the
        // FPU/SSE unit enabled: CR0.EM clear, CR0.MP set, CR4.OSFXSR
        // and CR4.OSXMMEXCPT set, then a clean fninit.  The kernel
        // itself stays -mgeneral-regs-only and never touches xmm, so
        // there is no kernel SSE state to manage here.
        auto enable_sse() -> void {
            u64 cr0 = 0;
            u64 cr4 = 0;
            asm volatile("mov %%cr0, %0" : "=r"(cr0));
            cr0 &= ~(1ull << 2); // EM: no x87 emulation
            cr0 |= (1ull << 1);  // MP: monitor co-processor
            asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
            asm volatile("mov %%cr4, %0" : "=r"(cr4));
            cr4 |= (1ull << 9);  // OSFXSR: enable FXSAVE/FXRSTOR + SSE
            cr4 |= (1ull << 10); // OSXMMEXCPT: unmasked #XM
            asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
            asm volatile("fninit");
        }
    } // namespace

    auto init_gdt_tss() -> bool {
        auto stack = mm::heap::alloc(16 * 1024);
        if (!stack) {
            return false;
        }
        kernel_stack          = stack.value;
        task_state            = tss{};
        task_state.iomap_base = sizeof(tss);

        gdt[0] = 0;
        gdt[1] = 0x00AF9A000000FFFFull; // ring-0 64-bit code
        gdt[2] = 0x00AF92000000FFFFull; // ring-0 data
        gdt[3] = 0x00AFF2000000FFFFull; // ring-3 data
        gdt[4] = 0x00AFFA000000FFFFull; // ring-3 64-bit code
        tss_descriptor(reinterpret_cast<u64>(&task_state), sizeof(tss) - 1);

        descriptor.limit = sizeof(gdt) - 1;
        descriptor.base  = reinterpret_cast<u64>(&gdt[0]);
        set_kernel_stack(reinterpret_cast<u64>(kernel_stack) + 16 * 1024);
        astia_load_gdt_tss(&descriptor, tss_selector);

        constexpr u32 msr_efer  = 0xC0000080;
        constexpr u32 msr_star  = 0xC0000081;
        constexpr u32 msr_lstar = 0xC0000082;
        constexpr u32 msr_fmask = 0xC0000084;
        // SYSCALL enable (SCE) plus NX execution-disable (NXE): page_map
        // emits PTE bit 63 for page_flags::no_execute, and bit 63 is a
        // *reserved* bit until NXE is on — mmap marks its pages NX.
        wrmsr(msr_efer, rdmsr(msr_efer) | 1ull | (1ull << 11));
        wrmsr(msr_star, (0x20ull << 48) | (static_cast<u64>(kernel_code_selector) << 32));
        wrmsr(msr_lstar, reinterpret_cast<u64>(&astia_syscall_entry));
        wrmsr(msr_fmask, (1ull << 8) | (1ull << 9) | (1ull << 10));
        enable_sse();
        return true;
    }

    auto set_kernel_stack(u64 stack_top) -> void {
        task_state.rsp0         = stack_top;
        astia_syscall_stack_top = stack_top;
    }

} // namespace astia::arch::x86_64
