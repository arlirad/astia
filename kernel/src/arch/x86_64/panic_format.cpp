#include "../../dbg/record.hpp"
#include "panic_state.hpp"

namespace astia::arch::x86_64 {
    void format_panic_registers(const panic_state& state, dbg::sink_fn sink,
                                void* context) {
        dbg::record line{};

        const interrupt_frame& frame = state.frame;

        line.add("kpanic: fault vector=");
        line.add_hex(frame.vector);
        line.add(" error=");
        line.add_hex(frame.error);
        line.add(" rip=");
        line.add_hex(frame.rip);
        line.add(" cr0=");
        line.add_hex(state.cr0);
        line.add(" cr2=");
        line.add_hex(state.cr2);
        line.add(" cr3=");
        line.add_hex(state.cr3);
        line.add(" cr4=");
        line.add_hex(state.cr4);
        sink(context, line.text, line.length);

        line.clear();
        line.add("kpanic: rip=");
        line.add_hex(frame.rip);
        line.add(" rbp=");
        line.add_hex(frame.rbp);
        sink(context, line.text, line.length);

        line.clear();
        line.add("kpanic: regs rax=");
        line.add_hex(frame.rax);
        line.add(" rbx=");
        line.add_hex(frame.rbx);
        line.add(" rcx=");
        line.add_hex(frame.rcx);
        line.add(" rdx=");
        line.add_hex(frame.rdx);
        line.add(" rsi=");
        line.add_hex(frame.rsi);
        line.add(" rdi=");
        line.add_hex(frame.rdi);
        line.add(" rbp=");
        line.add_hex(frame.rbp);
        line.add(" rsp=");
        line.add_hex(frame.rsp);
        line.add(" r8=");
        line.add_hex(frame.r8);
        line.add(" r9=");
        line.add_hex(frame.r9);
        line.add(" r10=");
        line.add_hex(frame.r10);
        line.add(" r11=");
        line.add_hex(frame.r11);
        line.add(" r12=");
        line.add_hex(frame.r12);
        line.add(" r13=");
        line.add_hex(frame.r13);
        line.add(" r14=");
        line.add_hex(frame.r14);
        line.add(" r15=");
        line.add_hex(frame.r15);
        line.add(" rflags=");
        line.add_hex(frame.rflags);
        sink(context, line.text, line.length);
    }
} // namespace astia::arch::x86_64
