#include "panic_format.hpp"

#include "../../dbg/record.hpp"

namespace astia::arch::aarch64 {
    namespace {
        void footer(dbg::sink_fn sink, void* context, dbg::record& line) {
            sink(context, line.text, line.length);
            line.clear();
        }

        void add_x_name(dbg::record& line, u32 index) {
            char name[5]{};

            name[0] = 'x';

            if (index >= 10) {
                name[1] = static_cast<char>('0' + index / 10);
                name[2] = static_cast<char>('0' + index % 10);
                name[3] = ':';
            }
            else {
                name[1] = static_cast<char>('0' + index);
                name[2] = ':';
            }

            line.add_padded(name, 4);
            line.add(' ');
        }

        void row(dbg::record& line, dbg::sink_fn sink, void* context,
                 const exception_frame& frame, u32 first, u32 count) {
            line.add("[PANIC]  ");

            for (u32 slot = 0; slot < 4; ++slot) {
                const u32 index = first + slot;

                if (slot < count) {
                    add_x_name(line, index);
                    line.add_hex_padded(frame.x[index]);
                }
                else {
                    line.add_padded("", 4);
                    line.add_padded("", 18);
                }

                if (slot != 3)
                    line.add("  ");
            }

            footer(sink, context, line);
        }
    } // namespace

    void format_panic_registers(const exception_frame& frame, u64 interrupted_sp,
                                u64 ttbr0, u64 vector_kind, dbg::sink_fn sink,
                                void* context) {
        dbg::record line{};

        line.add("[PANIC] fault vector-kind=");
        line.add_dec(vector_kind);
        line.add(" elr=");
        line.add_hex(frame.elr_el1);
        line.add(" esr=");
        line.add_hex(frame.esr_el1);
        line.add(" far=");
        line.add_hex(frame.far_el1);
        line.add(" spsr=");
        line.add_hex(frame.spsr_el1);
        footer(sink, context, line);

        line.add("[PANIC] registers:");
        footer(sink, context, line);

        for (u32 first = 0; first < exception_frame_gpr_count; first += 4) {
            const u32 remaining = exception_frame_gpr_count - first;

            row(line, sink, context, frame, first, remaining < 4 ? remaining : 4);
        }

        line.add("[PANIC]  ");
        line.add_padded("sp:", 4);
        line.add(' ');
        line.add_hex_padded(interrupted_sp);
        line.add("  ");
        line.add_padded("elr:", 4);
        line.add(' ');
        line.add_hex_padded(frame.elr_el1);
        line.add("  ");
        line.add_padded("spsr:", 5);
        line.add(' ');
        line.add_hex_padded(frame.spsr_el1);
        line.add("  ");
        line.add_padded("far:", 4);
        line.add(' ');
        line.add_hex_padded(frame.far_el1);
        footer(sink, context, line);

        line.add("[PANIC]  ");
        line.add_padded("esr:", 4);
        line.add(' ');
        line.add_hex_padded(frame.esr_el1);
        line.add("  ");
        line.add_padded("ttbr0:", 6);
        line.add(' ');
        line.add_hex_padded(ttbr0);
        footer(sink, context, line);
    }
} // namespace astia::arch::aarch64
