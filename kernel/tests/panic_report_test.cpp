#include <astia/arch/exception_frame.hpp>
#include <astia/dbg/panic_report.hpp>

#include "panic_format.hpp"
#include "panic_state.hpp"
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
        value.arch               = "x86_64";
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

    astia::arch::x86_64::panic_state x86_state() {
        astia::arch::x86_64::panic_state value{};

        value.frame.rax    = 0x100;
        value.frame.rbx    = 0x101;
        value.frame.rcx    = 0x102;
        value.frame.rdx    = 0x103;
        value.frame.rsi    = 0x104;
        value.frame.rdi    = 0x105;
        value.frame.rbp    = 0x7100;
        value.frame.rsp    = 0x7000;
        value.frame.r8     = 0x108;
        value.frame.r9     = 0x109;
        value.frame.r10    = 0x10a;
        value.frame.r11    = 0x10b;
        value.frame.r12    = 0x10c;
        value.frame.r13    = 0x10d;
        value.frame.r14    = 0x10e;
        value.frame.r15    = 0x10f;
        value.frame.vector = 14;
        value.frame.error  = 2;
        value.frame.rip    = 0xdeadbeef;
        value.frame.rflags = 0x202;
        value.cr0          = 0x80000001;
        value.cr2          = 0x4000;
        value.cr3          = 0x5000;
        value.cr4          = 0x20;

        return value;
    }

    astia::arch::aarch64::exception_frame aarch64_frame() {
        astia::arch::aarch64::exception_frame value{};

        for (astia::u32 i = 0; i < astia::arch::aarch64::exception_frame_gpr_count; ++i) {
            value.x[i] = 0x200 + i;
        }

        value.elr_el1  = 0x40400000;
        value.spsr_el1 = 0x3c5;
        value.esr_el1  = 0xf2001234;
        value.far_el1  = 0x1000;

        return value;
    }
} // namespace

ASTIA_TEST(panic_report_common_records) {
    capture output;

    astia::dbg::format_panic_context(process(), sink, &output);

    ASTIA_CHECK(output.contains("kpanic: cpu=3 arch=x86_64"));
    ASTIA_CHECK(output.contains("kpanic: proc pid=1 tid=2 name=\"init\" state=runnable"));
    ASTIA_CHECK(output.contains("uid=10 euid=11 gid=12 egid=13 pgrp=14 session=15"));
    ASTIA_CHECK(output.contains("kpanic: thread state=running kstack-top=0x8000"));
    ASTIA_CHECK(output.contains("kstack-size=0x4000 kstack-high-water=0x1200"));
    ASTIA_CHECK(output.contains("address-space=0x9000"));
}

ASTIA_TEST(panic_report_x86_records_every_register) {
    capture output;

    astia::arch::x86_64::format_panic_registers(x86_state(), sink, &output);

    ASTIA_CHECK(output.contains("kpanic: fault vector=0xe error=0x2 rip=0xdeadbeef"));
    ASTIA_CHECK(output.contains("cr0=0x80000001 cr2=0x4000 cr3=0x5000 cr4=0x20"));
    ASTIA_CHECK(output.contains("kpanic: rip=0xdeadbeef rbp=0x7100"));
    ASTIA_CHECK(output.contains("kpanic: regs rax=0x100 rbx=0x101 rcx=0x102"));
    ASTIA_CHECK(output.contains("r15=0x10f rflags=0x202"));
}

ASTIA_TEST(panic_report_aarch64_records_every_register) {
    capture output;

    astia::arch::aarch64::format_panic_registers(aarch64_frame(), 0x8000, 0x9000, 4, sink,
                                                 &output);

    ASTIA_CHECK(output.contains("kpanic: fault vector-kind=4 elr=0x40400000"));
    ASTIA_CHECK(output.contains("spsr=0x3c5 esr=0xf2001234 far=0x1000 ttbr0=0x9000"));
    ASTIA_CHECK(output.contains("kpanic: regs x0=0x200 x1=0x201"));
    ASTIA_CHECK(output.contains("x7=0x207 sp=0x8000"));
    ASTIA_CHECK(output.contains("x8=0x208"));
    ASTIA_CHECK(output.contains("x24=0x218 x25=0x219"));
    ASTIA_CHECK(output.contains("x30=0x21e"));
}

ASTIA_TEST(panic_report_reports_unavailable_context) {
    capture                   output;
    astia::dbg::panic_process empty{};

    astia::dbg::format_panic_context(empty, sink, &output);

    ASTIA_CHECK(output.contains("kpanic: proc unavailable"));
    ASTIA_CHECK(output.contains("kpanic: thread unavailable"));

    astia::dbg::panic_process locked = process();

    locked.metadata_available = false;

    capture locked_output;

    astia::dbg::format_panic_context(locked, sink, &locked_output);
    ASTIA_CHECK(locked_output.contains("uid=unavailable euid=unavailable"));
}

ASTIA_TEST(panic_report_keeps_records_bounded) {
    capture output;

    astia::arch::aarch64::format_panic_registers(aarch64_frame(), 0x8000, 0x9000, 4, sink,
                                                 &output);

    ASTIA_CHECK(output.lines >= 6);
    ASTIA_CHECK(output.max_line < 512);
}

ASTIA_TEST(panic_report_bounds_process_name) {
    capture                   output;
    astia::dbg::panic_process value = process();

    for (unsigned i = 0; i < sizeof value.name - 1; ++i) {
        value.name[i] = static_cast<char>('a' + i % 26);
    }

    value.name[sizeof value.name - 1] = '\0';

    astia::dbg::format_panic_context(value, sink, &output);
    ASTIA_CHECK(output.contains("name=\""));
    ASTIA_CHECK(output.length < sizeof output.text);
}
