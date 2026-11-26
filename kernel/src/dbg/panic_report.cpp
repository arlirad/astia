#include <astia/dbg/panic_report.hpp>

namespace astia::dbg {
    namespace {
        constexpr u32 record_capacity = 512;

        struct record {
            char text[record_capacity]{};
            u32  length = 0;

            void clear() {
                length = 0;
            }

            void add(char value) {
                if (length + 1 < record_capacity)
                    text[length++] = value;
            }

            void add(const char* value) {
                for (u32 i = 0; value != nullptr && value[i] != '\0'; ++i) {
                    add(value[i]);
                }
            }

            void add_dec(u64 value) {
                char digits[20];
                u32  count = 0;

                if (value == 0) {
                    add('0');

                    return;
                }

                while (value != 0 && count < sizeof digits) {
                    digits[count++] = static_cast<char>('0' + value % 10);
                    value /= 10;
                }

                while (count != 0) {
                    add(digits[--count]);
                }
            }

            void add_hex(u64 value) {
                add("0x");

                char digits[16];
                u32  count = 0;

                if (value == 0) {
                    add('0');

                    return;
                }

                while (value != 0 && count < sizeof digits) {
                    const u32 digit = static_cast<u32>(value & 0xfu);

                    digits[count++] =
                        static_cast<char>(digit < 10 ? '0' + digit : 'a' + digit - 10);
                    value >>= 4;
                }

                while (count != 0) {
                    add(digits[--count]);
                }
            }

            void flush(sink_fn sink, void* context) {
                if (sink != nullptr && length != 0)
                    sink(context, text, length);

                clear();
            }
        };

        constexpr const char* x86_names[16] = {
            "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
            "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        };

        void append_x86_regs(record& line, const panic_registers& regs, sink_fn sink,
                             void* context) {
            line.add("kpanic: regs");

            for (u32 i = 0; i < 16 && i < regs.gpr_count; ++i) {
                line.add(' ');
                line.add(x86_names[i]);
                line.add('=');
                line.add_hex(regs.gpr[i]);
            }

            line.add(" rsp=");
            line.add_hex(regs.rsp);
            line.add(" rbp=");
            line.add_hex(regs.rbp);
            line.add(" rflags=");
            line.add_hex(regs.rflags);
            line.flush(sink, context);
        }

        void append_aarch64_regs(record& line, const panic_registers& regs, sink_fn sink,
                                 void* context) {
            for (u32 base = 0; base < regs.gpr_count; base += 8) {
                line.add("kpanic: regs");

                const u32 end = base + 8 < regs.gpr_count ? base + 8 : regs.gpr_count;

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
                    line.add_hex(regs.gpr[i]);
                }

                if (base == 0) {
                    line.add(" sp=");
                    line.add_hex(regs.rsp);
                }

                line.flush(sink, context);
            }
        }
    } // namespace

    void format_panic_report(const panic_registers& regs, const panic_process& process,
                             sink_fn sink, void* context) {
        record line{};

        line.add("kpanic: cpu=");
        line.add_dec(process.cpu);
        line.add(" arch=");
        line.add(regs.arch);
        line.flush(sink, context);

        if (!process.available) {
            line.add("kpanic: proc unavailable");
            line.flush(sink, context);
        }
        else {
            line.add("kpanic: proc pid=");
            line.add_dec(process.pid);
            line.add(" tid=");
            line.add_dec(process.tid);
            line.add(" name=\"");
            line.add(process.name);
            line.add("\" state=");
            line.add(process.state);

            if (process.metadata_available) {
                line.add(" uid=");
                line.add_dec(process.uid);
                line.add(" euid=");
                line.add_dec(process.euid);
                line.add(" gid=");
                line.add_dec(process.gid);
                line.add(" egid=");
                line.add_dec(process.egid);
                line.add(" pgrp=");
                line.add_dec(process.pgrp);
                line.add(" session=");
                line.add_dec(process.session);
            }
            else {
                line.add(" uid=unavailable euid=unavailable gid=unavailable "
                         "egid=unavailable pgrp=unavailable session=unavailable");
            }

            line.flush(sink, context);
        }

        if (!process.thread_available) {
            line.add("kpanic: thread unavailable");
            line.flush(sink, context);
        }
        else {
            line.add("kpanic: thread state=");
            line.add(process.thread_state);
            line.add(" kstack-top=");
            line.add_hex(process.kstack_top);
            line.add(" kstack-size=");
            line.add_hex(process.kstack_size);
            line.add(" kstack-high-water=");
            line.add_hex(process.kstack_high_water);
            line.add(" cpu=");
            line.add_dec(process.cpu);
            line.add(" address-space=");
            line.add_hex(process.address_space);
            line.flush(sink, context);
        }

        line.add("kpanic: fault");

        if (regs.aarch64) {
            line.add(" vector-kind=");
            line.add_dec(regs.vector_kind);
            line.add(" elr=");
            line.add_hex(regs.elr);
            line.add(" spsr=");
            line.add_hex(regs.spsr);
            line.add(" esr=");
            line.add_hex(regs.esr);
            line.add(" far=");
            line.add_hex(regs.far);
            line.add(" ttbr0=");
            line.add_hex(regs.ttbr0);
        }
        else if (regs.x86) {
            line.add(" vector=");
            line.add_hex(regs.vector);
            line.add(" error=");
            line.add_hex(regs.error);
            line.add(" rip=");
            line.add_hex(regs.rip);
            line.add(" cr0=");
            line.add_hex(regs.cr0);
            line.add(" cr2=");
            line.add_hex(regs.cr2);
            line.add(" cr3=");
            line.add_hex(regs.cr3);
            line.add(" cr4=");
            line.add_hex(regs.cr4);
        }
        else {
            line.add(" rip=");
            line.add_hex(regs.rip);
        }

        line.flush(sink, context);

        line.add("kpanic: rip=");
        line.add_hex(regs.rip);
        line.add(" rbp=");
        line.add_hex(regs.rbp);
        line.flush(sink, context);

        if (regs.aarch64)
            append_aarch64_regs(line, regs, sink, context);
        else
            append_x86_regs(line, regs, sink, context);
    }
} // namespace astia::dbg
