#include <astia/dbg/panic_report.hpp>

#include "../../dbg/stacktrace.hpp"
#include "panic_state.hpp"

namespace astia::arch {
    const char* panic_arch_name() {
        return "x86_64";
    }

    u64 panic_address_space_root() {
        u64 value = 0;

        asm volatile("mov %%cr3, %0" : "=r"(value));

        return value;
    }

    namespace {
        void capture_panic_state(x86_64::panic_state& out) {
            interrupt_frame& frame = out.frame;

            asm volatile("movq %%rax, %0" : "=m"(frame.rax));
            asm volatile("movq %%rbx, %0" : "=m"(frame.rbx));
            asm volatile("movq %%rcx, %0" : "=m"(frame.rcx));
            asm volatile("movq %%rdx, %0" : "=m"(frame.rdx));
            asm volatile("movq %%rsi, %0" : "=m"(frame.rsi));
            asm volatile("movq %%rdi, %0" : "=m"(frame.rdi));
            asm volatile("movq %%rbp, %0" : "=m"(frame.rbp));
            asm volatile("movq %%rsp, %0" : "=m"(frame.rsp));
            asm volatile("movq %%r8, %0" : "=m"(frame.r8));
            asm volatile("movq %%r9, %0" : "=m"(frame.r9));
            asm volatile("movq %%r10, %0" : "=m"(frame.r10));
            asm volatile("movq %%r11, %0" : "=m"(frame.r11));
            asm volatile("movq %%r12, %0" : "=m"(frame.r12));
            asm volatile("movq %%r13, %0" : "=m"(frame.r13));
            asm volatile("movq %%r14, %0" : "=m"(frame.r14));
            asm volatile("movq %%r15, %0" : "=m"(frame.r15));

            u64 flags = 0;

            asm volatile("pushfq; popq %0" : "=r"(flags));

            frame.rflags = flags;
            frame.rip    = reinterpret_cast<u64>(__builtin_return_address(0));
            frame.vector = 0;
            frame.error  = 0;

            asm volatile("mov %%cr0, %0" : "=r"(out.cr0));
            asm volatile("mov %%cr2, %0" : "=r"(out.cr2));
            asm volatile("mov %%cr4, %0" : "=r"(out.cr4));

            out.cr3 = panic_address_space_root();
        }
    } // namespace

    void panic_report_registers(dbg::sink_fn sink, void* context, const void* frame) {
        x86_64::panic_state state{};

        if (frame != nullptr) {
            state.frame = *static_cast<const interrupt_frame*>(frame);

            asm volatile("mov %%cr0, %0" : "=r"(state.cr0));
            asm volatile("mov %%cr2, %0" : "=r"(state.cr2));
            asm volatile("mov %%cr4, %0" : "=r"(state.cr4));

            state.cr3 = panic_address_space_root();
        }
        else {
            capture_panic_state(state);
        }

        x86_64::format_panic_registers(state, sink, context);
        dbg::stacktrace(state.frame.rbp, state.frame.rip);
    }
} // namespace astia::arch
