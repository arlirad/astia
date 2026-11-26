#include "panic_format.hpp"

#include "../../dbg/record.hpp"

namespace astia::arch::aarch64 {
    void format_panic_registers(const exception_frame& frame, u64 interrupted_sp,
                                u64 ttbr0, u64 vector_kind, dbg::sink_fn sink,
                                void* context) {
        dbg::record line{};

        line.add("kpanic: fault vector-kind=");
        line.add_dec(vector_kind);
        line.add(" elr=");
        line.add_hex(frame.elr_el1);
        line.add(" spsr=");
        line.add_hex(frame.spsr_el1);
        line.add(" esr=");
        line.add_hex(frame.esr_el1);
        line.add(" far=");
        line.add_hex(frame.far_el1);
        line.add(" ttbr0=");
        line.add_hex(ttbr0);
        sink(context, line.text, line.length);

        line.clear();
        line.add("kpanic: rip=");
        line.add_hex(frame.elr_el1);
        line.add(" rbp=");
        line.add_hex(frame.x[29]);
        sink(context, line.text, line.length);

        for (u32 base = 0; base < exception_frame_gpr_count; base += 8) {
            line.clear();
            line.add("kpanic: regs");

            const u32 end = base + 8 < exception_frame_gpr_count
                                ? base + 8
                                : static_cast<u32>(exception_frame_gpr_count);

            for (u32 i = base; i < end; ++i) {
                char name[4] = {'x', '\0', '\0', '\0'};

                if (i >= 10) {
                    name[1] = static_cast<char>('0' + i / 10);
                    name[2] = static_cast<char>('0' + i % 10);
                }
                else {
                    name[1] = static_cast<char>('0' + i);
                }

                line.add(' ');
                line.add(name);
                line.add('=');
                line.add_hex(frame.x[i]);
            }

            if (base == 0) {
                line.add(" sp=");
                line.add_hex(interrupted_sp);
            }

            sink(context, line.text, line.length);
        }
    }
} // namespace astia::arch::aarch64
