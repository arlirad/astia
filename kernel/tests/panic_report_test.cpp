#include <astia/dbg/panic_report.hpp>

#include "support/test.hpp"

#include <cstring>

namespace {
    struct capture {
        char          text[16384]{};
        unsigned long length   = 0;
        unsigned int  lines    = 0;
        unsigned long max_line = 0;

        bool contains(const char* needle) const {
            return std::strstr(text, needle) != nullptr;
        }
    };

    void sink(void* context, const char* text, astia::u32 length) {
        auto& output = *static_cast<capture*>(context);

        for (astia::u32 i = 0; i < length && output.length + 2 < sizeof output.text;
             ++i) {
            output.text[output.length++] = text[i];
        }

        output.text[output.length++] = '\n';
        output.text[output.length]   = '\0';

        if (length > output.max_line)
            output.max_line = length;

        ++output.lines;
    }

    astia::dbg::panic_process process() {
        astia::dbg::panic_process value{};

        value.available          = true;
        value.metadata_available = true;
        value.thread_available   = true;
        value.pid                = 1;
        value.tid                = 2;
        value.cpu                = 3;

        std::strcpy(value.name, "init");

        value.state             = "runnable";
        value.thread_state      = "running";
        value.uid               = 10;
        value.euid              = 11;
        value.gid               = 12;
        value.egid              = 13;
        value.pgrp              = 14;
        value.session           = 15;
        value.kstack_top        = 0x8000;
        value.kstack_size       = 0x4000;
        value.kstack_high_water = 0x1200;
        value.address_space     = 0x9000;

        return value;
    }

    astia::dbg::panic_registers x86_registers() {
        astia::dbg::panic_registers value{};

        value.arch      = "x86_64";
        value.x86       = true;
        value.gpr_count = 16;

        for (astia::u32 i = 0; i < 16; ++i) {
            value.gpr[i] = 0x100 + i;
        }

        value.rip    = 0xdeadbeef;
        value.rsp    = 0x7000;
        value.rbp    = 0x7100;
        value.rflags = 0x202;
        value.vector = 14;
        value.error  = 2;
        value.cr0    = 0x80000001;
        value.cr2    = 0x4000;
        value.cr3    = 0x5000;
        value.cr4    = 0x20;

        return value;
    }

    astia::dbg::panic_registers aarch64_registers() {
        astia::dbg::panic_registers value{};

        value.arch      = "aarch64";
        value.aarch64   = true;
        value.gpr_count = 31;

        for (astia::u32 i = 0; i < 31; ++i) {
            value.gpr[i] = 0x200 + i;
        }

        value.rsp         = 0x8000;
        value.elr         = 0x40400000;
        value.spsr        = 0x3c5;
        value.esr         = 0xf2001234;
        value.far         = 0x1000;
        value.ttbr0       = 0x9000;
        value.vector_kind = 4;

        return value;
    }
} // namespace

ASTIA_TEST(panic_report_x86_records_every_register) {
    capture output;

    astia::dbg::format_panic_report(x86_registers(), process(), sink, &output);

    ASTIA_CHECK(output.contains("kpanic: cpu=3 arch=x86_64"));
    ASTIA_CHECK(output.contains("kpanic: proc pid=1 tid=2 name=\"init\" state=runnable"));
    ASTIA_CHECK(output.contains("uid=10 euid=11 gid=12 egid=13 pgrp=14 session=15"));
    ASTIA_CHECK(output.contains("kpanic: thread state=running kstack-top=0x8000"));
    ASTIA_CHECK(output.contains("kstack-size=0x4000 kstack-high-water=0x1200"));
    ASTIA_CHECK(output.contains("address-space=0x9000"));
    ASTIA_CHECK(output.contains("kpanic: fault vector=0xe error=0x2 rip=0xdeadbeef"));
    ASTIA_CHECK(output.contains("cr0=0x80000001 cr2=0x4000 cr3=0x5000 cr4=0x20"));
    ASTIA_CHECK(output.contains("kpanic: regs rax=0x100 rbx=0x101 rcx=0x102"));
    ASTIA_CHECK(output.contains("r15=0x10f rsp=0x7000 rbp=0x7100 rflags=0x202"));
}

ASTIA_TEST(panic_report_aarch64_records_every_register) {
    capture output;

    astia::dbg::format_panic_report(aarch64_registers(), process(), sink, &output);

    ASTIA_CHECK(output.contains("kpanic: cpu=3 arch=aarch64"));
    ASTIA_CHECK(output.contains("kpanic: fault vector-kind=4 elr=0x40400000"));
    ASTIA_CHECK(output.contains("spsr=0x3c5 esr=0xf2001234 far=0x1000 ttbr0=0x9000"));
    ASTIA_CHECK(output.contains("kpanic: regs x0=0x200 x1=0x201"));
    ASTIA_CHECK(output.contains("x7=0x207 sp=0x8000"));
    ASTIA_CHECK(output.contains("x8=0x208"));
    ASTIA_CHECK(output.contains("x24=0x218 x25=0x219"));
    ASTIA_CHECK(output.contains("x24=0x218 x25=0x219"));
    ASTIA_CHECK(output.contains("x30=0x21e"));
}

ASTIA_TEST(panic_report_reports_unavailable_context) {
    capture                   output;
    astia::dbg::panic_process empty{};

    astia::dbg::format_panic_report(x86_registers(), empty, sink, &output);

    ASTIA_CHECK(output.contains("kpanic: proc unavailable"));
    ASTIA_CHECK(output.contains("kpanic: thread unavailable"));

    astia::dbg::panic_process locked = process();

    locked.metadata_available = false;

    capture locked_output;

    astia::dbg::format_panic_report(x86_registers(), locked, sink, &locked_output);
    ASTIA_CHECK(locked_output.contains("uid=unavailable euid=unavailable"));
}

ASTIA_TEST(panic_report_keeps_records_bounded) {
    capture output;

    astia::dbg::format_panic_report(aarch64_registers(), process(), sink, &output);
    ASTIA_CHECK(output.lines >= 8);
    ASTIA_CHECK(output.max_line < 512);
}

ASTIA_TEST(panic_report_bounds_process_name) {
    capture                   output;
    astia::dbg::panic_process value = process();

    for (unsigned i = 0; i < sizeof value.name - 1; ++i) {
        value.name[i] = static_cast<char>('a' + i % 26);
    }

    value.name[sizeof value.name - 1] = '\0';

    astia::dbg::format_panic_report(x86_registers(), value, sink, &output);
    ASTIA_CHECK(output.contains("name=\""));
    ASTIA_CHECK(std::strstr(output.text, "name=\"") != nullptr);
    ASTIA_CHECK(output.length < sizeof output.text);
}
