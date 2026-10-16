#include "lapic.hpp"

#include "layout.hpp"
#include "page_map.hpp"
#include "io.hpp"
#include "pit.hpp"

// xAPIC MMIO bring-up. The frequency of the LAPIC timer is a platform
// constant we don't assume (QEMU, KVM and real hardware all differ), so
// it is measured against a PIT one-shot of known length: run the LAPIC
// counter down from full scale while the PIT counts a known span, take
// the ratio, then program the periodic reload from the measured rate.

namespace astia::arch::x86_64 {

    namespace {

        constexpr u64 msr_apic_base = 0x1B;
        constexpr u64 apic_base_enabled = 1ull << 11; // global enable
        constexpr u64 apic_base_x2apic = 1ull << 10;  // MSR mode, not driven yet

        // Register offsets are byte offsets; the window is u32 dwords.
        constexpr u64 reg_eoi = 0x0B0 / 4;
        constexpr u64 reg_svr = 0x0F0 / 4;
        constexpr u64 reg_lvt_timer = 0x320 / 4;
        constexpr u64 reg_timer_initial = 0x380 / 4;
        constexpr u64 reg_timer_current = 0x390 / 4;
        constexpr u64 reg_timer_divide = 0x3E0 / 4;

        // LVT timer: bits 17..18 = mode (01 periodic, 00 one-shot), bit 16 = mask.
        constexpr u32 lvt_one_shot_masked = 1u << 16;
        constexpr u32 lvt_periodic = 1u << 17;
        constexpr u32 timer_divide_by_16 = 0x3;

        volatile u32* lapic_regs = nullptr; // null until start() maps it

        auto reg_write(u64 reg, u32 value) -> void {
            lapic_regs[reg] = value;
        }

        auto reg_read(u64 reg) -> u32 {
            return lapic_regs[reg];
        }

        /// The LAPIC timer's Hz, measured against a PIT one-shot; 0 if the
        /// counter did not move (broken bring-up).
        auto calibrate() -> u64 {
            constexpr u32 pit_span = pit::input_hz / 20; // ~50 ms
            pit::start_one_shot(pit_span);

            reg_write(reg_timer_divide, timer_divide_by_16);
            reg_write(reg_lvt_timer, lvt_one_shot_masked | lapic::timer_vector);
            reg_write(reg_timer_initial, 0xFFFFFFFF); // run down from full scale

            const u32 lapic_start = reg_read(reg_timer_current);
            while (pit::read_count() != 0) {
                // poll the PIT one-shot to zero; no interrupts involved
            }
            const u32 lapic_end = reg_read(reg_timer_current);

            if (lapic_end >= lapic_start) {
                return 0; // the LAPIC counter did not advance
            }
            return (static_cast<u64>(lapic_start - lapic_end) * pit::input_hz) / pit_span;
        }

    } // namespace

    auto lapic::start(u32 hz) -> bool {
        const u64 base_msr = rdmsr(msr_apic_base);
        if ((base_msr & apic_base_enabled) == 0 || (base_msr & apic_base_x2apic) != 0) {
            return false;
        }
        const u64 base = base_msr & 0xFFFFF000ull;
        // The LAPIC window is reserved MMIO, never covered by the direct
        // map, so map it ourselves — in the *shared kernel half*
        // (kernel_base + p), not at the raw physical address: the low
        // identity half is process-private in child address spaces, and
        // a timer EOI from ring-3 context would fault there.
        auto* map      = kernel_page_map();
        const u64 virt = kernel_base + base;
        if (map == nullptr ||
            !map->map_4k(virt, base,
                         page_flags::present | page_flags::writable |
                             page_flags::cache_disable)) {
            return false;
        }
        lapic_regs = reinterpret_cast<volatile u32*>(virt);

        reg_write(reg_svr, 0x1FF); // software-enable + spurious vec 0xFF

        const u64 measured = calibrate();
        if (measured == 0) {
            lapic_regs = nullptr;
            return false;
        }

        reg_write(reg_lvt_timer, lvt_one_shot_masked | timer_vector);
        reg_write(reg_timer_divide, timer_divide_by_16);
        reg_write(reg_timer_initial, static_cast<u32>(measured / hz));
        reg_write(reg_lvt_timer, lvt_periodic | timer_vector);
        return true;
    }

    auto lapic::eoi() -> void {
        if (lapic_regs != nullptr) {
            reg_write(reg_eoi, 0); // LAPIC-sourced: EOI goes to the LAPIC
        }
    }

} // namespace astia::arch::x86_64
