#include "../../dbg/record.hpp"
#include "panic_state.hpp"

namespace astia::arch::x86_64 {
    namespace {
        const char* vector_name(u64 vector) {
            static const char* const names[32] = {
                "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
                "#DF", "res", "#TS", "#NP", "#SS", "#GP", "#PF", "res",
                "#MF", "#AC", "#MC", "#XM", "#VE", "#CP", "res", "res",
                "res", "res", "res", "res", "res", "res", "#SX", "res",
            };

            return vector < 32 ? names[vector] : "unknown";
        }

        void footer(dbg::sink_fn sink, void* context, dbg::record& line) {
            sink(context, line.text, line.length);
            line.clear();
        }
    } // namespace

    void format_panic_registers(const panic_state& state, dbg::sink_fn sink,
                                void* context) {
        const interrupt_frame& frame = state.frame;

        dbg::record line{};

        if (state.fault) {
            line.add("[PANIC] fault ");
            line.add(vector_name(frame.vector));
            line.add(" (");
            line.add_dec(frame.vector);
            line.add(") error=");
            line.add_hex(frame.error);
            line.add(" rip=");
            line.add_hex(frame.rip);
        }
        else {
            line.add("[PANIC] pc rip=");
            line.add_hex(frame.rip);
        }

        footer(sink, context, line);

        line.add("[PANIC] registers:");
        footer(sink, context, line);

        const auto row = [&](const char* n0, u64 v0, const char* n1, u64 v1,
                             const char* n2, u64 v2, const char* n3, u64 v3) {
            line.add("[PANIC]  ");
            line.add_padded(n0, 4);
            line.add(' ');
            line.add_hex_padded(v0);
            line.add("  ");
            line.add_padded(n1, 4);
            line.add(' ');
            line.add_hex_padded(v1);
            line.add("  ");
            line.add_padded(n2, 4);
            line.add(' ');
            line.add_hex_padded(v2);
            line.add("  ");
            line.add_padded(n3, 4);
            line.add(' ');
            line.add_hex_padded(v3);
            footer(sink, context, line);
        };

        const auto row_cr = [&](const char* n0, u64 v0, const char* n1, u64 v1,
                                const char* n2, u64 v2, const char* n3, u64 v3) {
            line.add("[PANIC]  ");
            line.add_padded(n0, 4);
            line.add(' ');
            line.add_hex_padded(v0);
            line.add("  ");
            line.add_padded(n1, 4);
            line.add(' ');
            line.add_hex_padded(v1);
            line.add("  ");
            line.add_padded(n2, 4);
            line.add(' ');
            line.add_hex_padded(v2);
            line.add("  ");
            line.add_padded(n3, 4);
            line.add(' ');
            line.add_hex_padded(v3);
            footer(sink, context, line);
        };

        row("rax:", frame.rax, "rbx:", frame.rbx, "rcx:", frame.rcx, "rdx:", frame.rdx);
        row("rsi:", frame.rsi, "rdi:", frame.rdi, "rsp:", frame.rsp, "rbp:", frame.rbp);
        row("r8:", frame.r8, "r9:", frame.r9, "r10:", frame.r10, "r11:", frame.r11);
        row("r12:", frame.r12, "r13:", frame.r13, "r14:", frame.r14, "r15:", frame.r15);
        row("rip:", frame.rip, "rflags:", frame.rflags, "cs:", frame.cs, "ss:", frame.ss);
        row_cr("cr0:", state.cr0, "cr2:", state.cr2, "cr3:", state.cr3,
               "cr4:", state.cr4);
    }
} // namespace astia::arch::x86_64
