#include <astia/arch/cpu.hpp>
#include <astia/arch/exception_frame.hpp>
#include <astia/dbg/panic_report.hpp>

#include "board.hpp"
#include "gic.hpp"
#include "irq.hpp"
#include "panic_format.hpp"
#include "pl011.hpp"
#include "psci.hpp"
#include "timer.hpp"

extern "C" char __vector_table[];

namespace {
    using astia::u32;
    using astia::u64;
    using astia::arch::aarch64::exception_frame;
    using astia::plat::aarch64_virt::pl011_uart;

    constexpr u64 esr_ec_shift = 26;
    constexpr u64 esr_ec_mask  = 0x3f;
    constexpr u64 esr_il_bit   = 1ull << 25;
    constexpr u64 esr_iss_mask = 0x1ffffff;
    constexpr u64 ec_brk64     = 0x3c;
    constexpr u64 brk_imm_mask = 0xffff;

    [[nodiscard]] constexpr const char* vector_name(u64 kind) {
        switch (kind) {
        case 0:
            return "current-el-sp0-sync";
        case 1:
            return "current-el-sp0-irq";
        case 2:
            return "current-el-sp0-fiq";
        case 3:
            return "current-el-sp0-serror";
        case 4:
            return "current-el-spx-sync";
        case 5:
            return "current-el-spx-irq";
        case 6:
            return "current-el-spx-fiq";
        case 7:
            return "current-el-spx-serror";
        case 8:
            return "lower-el-aarch64-sync";
        case 9:
            return "lower-el-aarch64-irq";
        case 10:
            return "lower-el-aarch64-fiq";
        case 11:
            return "lower-el-aarch64-serror";
        case 12:
            return "lower-el-aarch32-sync";
        case 13:
            return "lower-el-aarch32-irq";
        case 14:
            return "lower-el-aarch32-fiq";
        case 15:
            return "lower-el-aarch32-serror";
        default:
            return "unknown";
        }
    }

    void write_field(const pl011_uart& console, const char* name, u64 value, u32 digits) {
        console.write(name);
        console.write("0x");
        console.write_hex(value, digits);
    }

    void write_line_field(const pl011_uart& console, const char* name, u64 value,
                          u32 digits) {
        write_field(console, name, value, digits);
        console.write_line("");
    }

    void panic_sink(void* context, const char* text, astia::u32 length) {
        const auto* console = static_cast<const pl011_uart*>(context);

        char line[512];

        const astia::u32 count = length < sizeof line - 1 ? length : sizeof line - 1;

        for (astia::u32 i = 0; i < count; ++i) {
            line[i] = text[i];
        }

        line[count] = '\0';

        console->write_line(line);
    }

    void finish_irq() {
        astia::arch::cpu::interrupt_exit();
        (void) astia::arch::aarch64::irq::run_handoff();
    }
} // namespace

extern "C" void astia_aarch64_install_exception_vectors() {
    const astia::u64 base = reinterpret_cast<astia::u64>(__vector_table);

    __asm__ volatile("msr vbar_el1, %0" : : "r"(base) : "memory");
    __asm__ volatile("isb" : : : "memory");
}

extern "C" void astia_aarch64_seed_far_el1(astia::u64 value) {
    __asm__ volatile("msr far_el1, %0" : : "r"(value) : "memory");
    __asm__ volatile("isb" : : : "memory");
}

extern "C" void astia_aarch64_irq_entry(const exception_frame* frame) {
    namespace gicv3 = astia::arch::aarch64::gicv3;
    namespace irq   = astia::arch::aarch64::irq;
    namespace timer = astia::arch::aarch64::timer;

    astia::arch::cpu::interrupt_enter();

    const irq::dispatch_result result = gicv3::acknowledge_and_complete();
    const pl011_uart           console{astia::plat::aarch64_virt::pl011_base};

    if (result.action == irq::dispatch_action::handled) {
        write_line_field(console, "irq: iar=", result.iar, 8);
        write_line_field(console, "irq: eoi=", result.intid, 8);
        write_line_field(console, "irq: dir=", result.intid, 8);

        if (result.intid == timer::ppi_id) {
            write_line_field(console, "irq: timer=", result.intid, 8);
            console.write_line("ASTIA AARCH64 VIRT BOOT: TIMER HANDLED");
            finish_irq();

            return;
        }

        console.write_line("ASTIA AARCH64 VIRT BOOT: IRQ HANDLED");
        finish_irq();

        return;
    }

    if (result.action == irq::dispatch_action::spurious) {
        console.write_line("irq: spurious");
        finish_irq();

        return;
    }

    console.write_line("");
    console.write_line("ASTIA AARCH64 VIRT BOOT: IRQ FATAL");
    write_line_field(console, "irq: intid=", result.intid, 8);
    write_line_field(console, "exception: elr=", frame->elr_el1, 16);
    write_line_field(console, "exception: spsr=", frame->spsr_el1, 16);
    console.write_line("ASTIA AARCH64 VIRT BOOT: EXCEPTION HALT");
    astia::plat::aarch64_virt::psci::system_off();
}

extern "C" [[noreturn]] void astia_aarch64_exception_entry(const exception_frame* frame,
                                                           astia::u64 vector_kind) {
    const pl011_uart console{astia::plat::aarch64_virt::pl011_base};

    const astia::u64 esr = frame->esr_el1;
    const astia::u64 ec  = (esr >> esr_ec_shift) & esr_ec_mask;
    const astia::u64 il  = (esr & esr_il_bit) != 0 ? 1 : 0;
    const astia::u64 iss = esr & esr_iss_mask;

    console.write_line("");

    if (vector_kind == astia::arch::aarch64::exception_vector_current_el_spx_sync)
        console.write_line("ASTIA AARCH64 VIRT BOOT: EXCEPTION SYNC");
    else
        console.write_line("ASTIA AARCH64 VIRT BOOT: EXCEPTION FATAL");

    console.write("exception: vector=");
    console.write_line(vector_name(vector_kind));

    write_field(console, "exception: esr=", esr, 16);
    write_field(console, " ec=", ec, 2);
    console.write(" il=");
    console.write_hex(il, 1);
    write_field(console, " iss=", iss, 8);
    console.write_line("");

    write_line_field(console, "exception: elr=", frame->elr_el1, 16);
    write_line_field(console, "exception: far=", frame->far_el1, 16);
    write_line_field(console, "exception: spsr=", frame->spsr_el1, 16);

    {
        astia::u64 ttbr0 = 0;

        asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));

        const astia::u64 interrupted_sp = reinterpret_cast<astia::u64>(frame) +
                                          astia::arch::aarch64::exception_frame_size;

        astia::dbg::panic_process process{};

        process.arch = "aarch64";
        process.cpu  = 0;

        astia::dbg::format_panic_context(process, panic_sink,
                                         const_cast<pl011_uart*>(&console));
        astia::arch::aarch64::format_panic_registers(*frame, interrupted_sp, ttbr0,
                                                     vector_kind, panic_sink,
                                                     const_cast<pl011_uart*>(&console));

        console.write_line("[PANIC] stack trace:");

        astia::u64 fp = frame->x[29];
        astia::u64 pc = frame->x[30];

        for (astia::u32 depth = 0; depth < 8; ++depth) {
            if (fp < 0x40000000ull || fp >= 0x50000000ull || (fp & 7u) != 0)
                break;

            console.write("[PANIC]  #");
            console.write_hex(depth, 1);
            console.write("  ");
            console.write("0x");
            console.write_hex(pc, 16);
            console.write("  rbp=");
            console.write("0x");
            console.write_hex(fp, 16);
            console.write_line("");

            const auto*      record = reinterpret_cast<const astia::u64*>(fp);
            const astia::u64 next   = record[0];

            pc = record[1];

            if (next <= fp)
                break;

            fp = next;
        }
    }

    if (vector_kind == astia::arch::aarch64::exception_vector_current_el_spx_sync) {
        if (ec == ec_brk64) {
            write_line_field(console, "exception: brk64 imm=", iss & brk_imm_mask, 4);
        }
        else {
            write_field(console, "exception: unexpected-sync ec=", ec, 2);
            console.write_line("");
        }
    }

    console.write_line("ASTIA AARCH64 VIRT BOOT: EXCEPTION HALT");
    astia::plat::aarch64_virt::psci::system_off();
}
