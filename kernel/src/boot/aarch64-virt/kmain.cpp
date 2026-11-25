#include <astia/arch/page_map.hpp>
#include <astia/boot/aarch64/fdt.hpp>
#include <astia/boot/aarch64/virt.hpp>

#include "board.hpp"
#include "gic.hpp"
#include "page_map_policy.hpp"
#include "pl011.hpp"
#include "psci.hpp"
#include "timer.hpp"

extern "C" char              __image_start[];
extern "C" char              __image_end[];
extern "C" char              __stack_bottom[];
extern "C" char              __stack_top[];
extern "C" char              __vector_table[];
extern "C" void              astia_aarch64_install_exception_vectors();
extern "C" void              astia_aarch64_seed_far_el1(astia::u64 value);
extern "C" [[noreturn]] void astia_aarch64_brk_probe();

namespace {
    using astia::arch::page_flags;
    using astia::arch::page_map;
    using astia::boot::aarch64::virt::boot_info;
    using astia::boot::aarch64::virt::console_source;
    using astia::plat::aarch64_virt::pl011_uart;

    namespace policy = astia::arch::aarch64::page_map_policy;
    namespace gicv3  = astia::arch::aarch64::gicv3;
    namespace timer  = astia::arch::aarch64::timer;

    constexpr page_flags ram_flags =
        page_flags::present | page_flags::writable | page_flags::no_execute;
    constexpr page_flags image_flags     = page_flags::present | page_flags::writable;
    constexpr page_flags read_only_flags = page_flags::present | page_flags::no_execute;
    constexpr page_flags stack_flags =
        page_flags::present | page_flags::writable | page_flags::no_execute;
    constexpr page_flags device_flags =
        page_flags::present | page_flags::writable | page_flags::device;

    [[nodiscard]] astia::u64 current_exception_level() {
        astia::u64 value = 0;
        __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
        return (value >> 2) & 0x3ULL;
    }

    [[nodiscard]] astia::u64 read_mair() {
        astia::u64 value = 0;
        __asm__ volatile("mrs %0, mair_el1" : "=r"(value));
        return value;
    }

    [[nodiscard]] astia::u64 read_tcr() {
        astia::u64 value = 0;
        __asm__ volatile("mrs %0, tcr_el1" : "=r"(value));
        return value;
    }

    [[nodiscard]] astia::u64 read_sctlr() {
        astia::u64 value = 0;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
        return value;
    }

    void write_field(const pl011_uart& console, const char* name, astia::u64 value,
                     astia::u32 digits) {
        console.write(name);
        console.write("0x");
        console.write_hex(value, digits);
    }

    void write_line_field(const pl011_uart& console, const char* name, astia::u64 value,
                          astia::u32 digits) {
        write_field(console, name, value, digits);
        console.write_line("");
    }

    void write_bit(const pl011_uart& console, const char* name, astia::u64 value,
                   astia::u32 bit) {
        console.write(name);
        console.write_hex((value >> bit) & 1ULL, 1);
    }

    void print_memory(const pl011_uart& console, const astia::mm::memory_map& regions,
                      const char* label) {
        for (astia::usize index = 0; index < regions.count(); ++index) {
            const astia::mm::memory_region& region = regions[index];
            console.write(label);
            write_field(console, "base=", region.base, 16);
            write_field(console, " size=", region.size, 16);
            console.write_line("");
        }
    }

    [[nodiscard]] constexpr astia::u64 align_down(astia::u64 value,
                                                  astia::u64 alignment) {
        return value & ~(alignment - 1);
    }

    [[nodiscard]] bool align_up(astia::u64 value, astia::u64 alignment,
                                astia::u64& result) {
        if (value > ~astia::u64{0} - (alignment - 1)) {
            return false;
        }
        result = (value + alignment - 1) & ~(alignment - 1);
        return true;
    }

    [[nodiscard]] bool add_checked(astia::u64 left, astia::u64 right,
                                   astia::u64& result) {
        if (left > ~astia::u64{0} - right) {
            return false;
        }
        result = left + right;
        return true;
    }

    [[nodiscard]] bool identity_range_supported(astia::u64 base, astia::u64 size) {
        if (size == 0) {
            return false;
        }

        astia::u64 end = 0;
        if (!add_checked(base, size, end)) {
            return false;
        }
        return base <= policy::max_virtual_address &&
               end - 1 <= policy::max_virtual_address &&
               base <= policy::max_physical_address &&
               end - 1 <= policy::max_physical_address;
    }

    [[nodiscard]] bool ranges_overlap(astia::u64 first, astia::u64 first_size,
                                      astia::u64 second, astia::u64 second_size) {
        astia::u64 first_end  = 0;
        astia::u64 second_end = 0;
        if (!add_checked(first, first_size, first_end) ||
            !add_checked(second, second_size, second_end)) {
            return true;
        }
        return first < second_end && second < first_end;
    }

    [[nodiscard]] bool map_identity_pages(page_map& map, astia::u64 base, astia::u64 size,
                                          page_flags flags) {
        if ((base & (policy::page_size - 1)) != 0 ||
            (size & (policy::page_size - 1)) != 0) {
            return false;
        }

        astia::u64 end = 0;
        if (!add_checked(base, size, end)) {
            return false;
        }
        for (astia::u64 address = base; address < end; address += policy::page_size) {
            if (!map.map_4k(address, address, flags)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool map_identity_ram(page_map& map, astia::u64 base, astia::u64 size) {
        if ((base & (policy::page_size - 1)) != 0 ||
            (size & (policy::page_size - 1)) != 0) {
            return false;
        }

        astia::u64 end = 0;
        if (!add_checked(base, size, end)) {
            return false;
        }

        astia::u64 address = base;
        while (address < end && (address & (policy::block_size_2m - 1)) != 0) {
            if (!map.map_4k(address, address, ram_flags)) {
                return false;
            }
            address += policy::page_size;
        }
        while (end - address >= policy::block_size_2m) {
            if (!map.map_2m(address, address, ram_flags)) {
                return false;
            }
            address += policy::block_size_2m;
        }
        while (address < end) {
            if (!map.map_4k(address, address, ram_flags)) {
                return false;
            }
            address += policy::page_size;
        }
        return true;
    }

    [[nodiscard]] bool range_within(const astia::mm::memory_map& regions, astia::u64 base,
                                    astia::u64 size) {
        astia::u64 end = 0;
        if (!add_checked(base, size, end)) {
            return false;
        }
        for (astia::usize index = 0; index < regions.count(); ++index) {
            const astia::mm::memory_region& region = regions[index];
            if (region.type != astia::mm::memory_type::usable) {
                continue;
            }
            astia::u64 region_end = 0;
            if (!add_checked(region.base, region.size, region_end)) {
                continue;
            }
            if (base >= region.base && end <= region_end) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool gic_conflicts_with_ram(const astia::mm::memory_map& regions,
                                              astia::u64 base, astia::u64 size) {
        for (astia::usize index = 0; index < regions.count(); ++index) {
            const astia::mm::memory_region& region = regions[index];
            if (region.type != astia::mm::memory_type::usable) {
                continue;
            }
            if (ranges_overlap(base, size, region.base, region.size)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool
    gic_ranges_supported(const astia::boot::aarch64::virt::gic_info& gic) {
        constexpr astia::u64 gicd_size_limit = 0x00100000;
        constexpr astia::u64 gicr_size_limit = 0x01000000;
        return gic.present && gic.distributor_size >= gicv3::minimum_distributor_size &&
               gic.distributor_size <= gicd_size_limit &&
               gic.redistributor_size >= gicv3::minimum_redistributor_size &&
               gic.redistributor_size <= gicr_size_limit &&
               (gic.distributor_base & (policy::page_size - 1)) == 0 &&
               (gic.distributor_size & (policy::page_size - 1)) == 0 &&
               (gic.redistributor_base & (policy::page_size - 1)) == 0 &&
               (gic.redistributor_size & (policy::page_size - 1)) == 0;
    }

    [[noreturn]] void fail(const pl011_uart& console, const char* reason) {
        console.write("reason: ");
        console.write_line(reason);
        console.write_line("ASTIA AARCH64 VIRT BOOT: FAIL");
        astia::plat::aarch64_virt::psci::system_off();
    }
} // namespace

extern "C" [[noreturn]] void astia_aarch64_early_main(astia::u64 entry_x0) {
    using namespace astia::boot::aarch64;
    using namespace astia::plat::aarch64_virt;

    const pl011_uart early{pl011_base};
    early.write_line("");
    early.write_line("ASTIA AARCH64 VIRT BOOT: EARLY STAGE");
    write_line_field(early, "entry-x0: ", entry_x0, 16);

    const astia::u64 exception_level = current_exception_level();
    write_line_field(early, "current-el: ", exception_level, 1);
    if (exception_level != 1) {
        fail(early, "unsupported exception level, expected EL1");
    }

    virt::boot_info  info{};
    const fdt::error parse_error =
        fdt::parse(reinterpret_cast<const astia::u8*>(dtb_address), dtb_window_size,
                   fdt::console_policy{pl011_base, pl011_size}, info);
    if (parse_error != fdt::error::none) {
        early.write("dtb: error=");
        early.write_line(fdt::describe(parse_error));
        fail(early, "device tree validation failed");
    }

    if (info.console.source == console_source::platform_default) {
        info.console.base = pl011_base;
        info.console.size = pl011_size;
    }

    const pl011_uart console{info.console.base == 0 ? pl011_base : info.console.base};
    write_field(console, "dtb: address=", dtb_address, 8);
    write_line_field(console, " size=", info.dtb_size, 8);

    print_memory(console, info.memory, "memory: ");
    print_memory(console, info.reserved, "reserved: ");

    write_field(console, "console: base=", info.console.base, 16);
    write_field(console, " size=", info.console.size, 8);
    console.write_line(info.console.source == console_source::device_tree
                           ? " source=device-tree"
                           : " source=platform-default");
    if (info.console.path[0] != '\0') {
        console.write("console-path: ");
        console.write_line(info.console.path);
    }

    console.write_line("ASTIA AARCH64 VIRT BOOT: HANDOFF OK");

    const astia::u64 image_start =
        align_down(reinterpret_cast<astia::u64>(__image_start), policy::page_size);
    astia::u64 image_end = 0;
    if (!align_up(reinterpret_cast<astia::u64>(__image_end), policy::page_size,
                  image_end) ||
        image_end <= image_start) {
        fail(console, "image range is invalid");
    }
    const astia::u64 image_size = image_end - image_start;

    const astia::u64 vector_start =
        align_down(reinterpret_cast<astia::u64>(__vector_table), policy::page_size);
    if (reinterpret_cast<astia::u64>(__vector_table) < image_start ||
        reinterpret_cast<astia::u64>(__vector_table) >= image_end) {
        fail(console, "exception vector table is outside the kernel image");
    }

    const astia::u64 stack_start =
        align_down(reinterpret_cast<astia::u64>(__stack_bottom), policy::page_size);
    astia::u64 stack_end = 0;
    if (!align_up(reinterpret_cast<astia::u64>(__stack_top), policy::page_size,
                  stack_end) ||
        stack_end <= stack_start) {
        fail(console, "stack range is invalid");
    }
    const astia::u64 stack_size = stack_end - stack_start;

    astia::u64 dtb_size = 0;
    if (!align_up(info.dtb_size, policy::page_size, dtb_size) || dtb_size == 0 ||
        dtb_size > dtb_window_size) {
        fail(console, "DTB range is outside the boot window");
    }
    if (info.dtb != reinterpret_cast<const astia::u8*>(dtb_address)) {
        fail(console, "DTB is not at the trusted RAM base");
    }
    if (!identity_range_supported(image_start, image_size) ||
        !identity_range_supported(stack_start, stack_size) ||
        !identity_range_supported(dtb_address, dtb_size) ||
        !identity_range_supported(info.console.base, pl011_size)) {
        fail(console, "identity-map range exceeds the address policy");
    }
    if (ranges_overlap(dtb_address, dtb_size, image_start, image_size)) {
        fail(console, "DTB overlaps the kernel image");
    }
    if (ranges_overlap(info.console.base, pl011_size, image_start, image_size)) {
        fail(console, "PL011 overlaps the kernel image");
    }
    if (!range_within(info.memory, image_start, image_size)) {
        fail(console, "kernel image is outside usable RAM");
    }

    page_map map = page_map::create();
    if (!map.valid()) {
        fail(console, "page-table arena allocation failed");
    }

    for (astia::usize index = 0; index < info.memory.count(); ++index) {
        const astia::mm::memory_region& region = info.memory[index];
        if (region.type != astia::mm::memory_type::usable) {
            continue;
        }
        if (!identity_range_supported(region.base, region.size) ||
            !map_identity_ram(map, region.base, region.size)) {
            fail(console, "usable RAM identity mapping failed");
        }
    }

    if (!map_identity_pages(map, dtb_address, dtb_size, read_only_flags)) {
        fail(console, "DTB identity mapping failed");
    }
    if (!map_identity_pages(map, image_start, image_size, image_flags)) {
        fail(console, "kernel image identity mapping failed");
    }
    if (!map_identity_pages(map, stack_start, stack_size, stack_flags)) {
        fail(console, "kernel stack identity mapping failed");
    }
    if (!map_identity_pages(map, info.console.base, pl011_size, device_flags)) {
        fail(console, "PL011 identity mapping failed");
    }

    if (!gic_ranges_supported(info.gic)) {
        fail(console, "GICv3 ranges are missing or invalid");
    }
    if (!identity_range_supported(info.gic.distributor_base, info.gic.distributor_size) ||
        !identity_range_supported(info.gic.redistributor_base,
                                  info.gic.redistributor_size)) {
        fail(console, "GICv3 range exceeds the address policy");
    }
    if (gic_conflicts_with_ram(info.memory, info.gic.distributor_base,
                               info.gic.distributor_size) ||
        gic_conflicts_with_ram(info.memory, info.gic.redistributor_base,
                               info.gic.redistributor_size)) {
        fail(console, "GICv3 range overlaps usable RAM");
    }
    if (ranges_overlap(info.gic.distributor_base, info.gic.distributor_size,
                       info.gic.redistributor_base, info.gic.redistributor_size)) {
        fail(console, "GICv3 distributor overlaps the redistributor");
    }
    if (ranges_overlap(info.gic.distributor_base, info.gic.distributor_size, image_start,
                       image_size) ||
        ranges_overlap(info.gic.redistributor_base, info.gic.redistributor_size,
                       image_start, image_size) ||
        ranges_overlap(info.gic.distributor_base, info.gic.distributor_size, dtb_address,
                       dtb_size) ||
        ranges_overlap(info.gic.redistributor_base, info.gic.redistributor_size,
                       dtb_address, dtb_size) ||
        ranges_overlap(info.gic.distributor_base, info.gic.distributor_size, stack_start,
                       stack_size) ||
        ranges_overlap(info.gic.redistributor_base, info.gic.redistributor_size,
                       stack_start, stack_size) ||
        ranges_overlap(info.gic.distributor_base, info.gic.distributor_size,
                       info.console.base, pl011_size) ||
        ranges_overlap(info.gic.redistributor_base, info.gic.redistributor_size,
                       info.console.base, pl011_size)) {
        fail(console, "GICv3 range overlaps a trusted boot range");
    }
    if (!map_identity_pages(map, info.gic.distributor_base, info.gic.distributor_size,
                            device_flags) ||
        !map_identity_pages(map, info.gic.redistributor_base, info.gic.redistributor_size,
                            device_flags)) {
        fail(console, "GICv3 identity mapping failed");
    }

    if (map.translate(dtb_address) != dtb_address ||
        map.translate(image_start) != image_start ||
        map.translate(vector_start) != vector_start ||
        map.translate(stack_start) != stack_start ||
        map.translate(info.console.base) != info.console.base ||
        map.translate(info.gic.distributor_base) != info.gic.distributor_base ||
        map.translate(info.gic.redistributor_base) != info.gic.redistributor_base ||
        !map.writable(info.console.base) || map.executable(info.console.base) ||
        !map.writable(info.gic.distributor_base) ||
        map.executable(info.gic.distributor_base) ||
        !map.writable(info.gic.redistributor_base) ||
        map.executable(info.gic.redistributor_base) || !map.executable(image_start) ||
        !map.executable(vector_start)) {
        fail(console, "identity map verification failed");
    }

    *astia::arch::kernel_page_map() = static_cast<page_map&&>(map);
    astia::arch::kernel_page_map()->activate();

    console.write_line("ASTIA AARCH64 VIRT BOOT: MMU ON");
    write_field(console, "mmu: mair=", read_mair(), 16);
    write_field(console, " tcr=", read_tcr(), 16);
    console.write_line("");

    const astia::u64 sctlr = read_sctlr();
    write_field(console, "mmu: sctlr=", sctlr, 16);
    write_bit(console, " m=", sctlr, 0);
    write_bit(console, " c=", sctlr, 2);
    write_bit(console, " i=", sctlr, 12);
    console.write_line("");

    console.write_line("mmu: pl011 write=ok");
    write_field(console, "mmu: pl011 read=", console.read_flags(), 8);
    console.write_line("");
    console.write_line("ASTIA AARCH64 VIRT BOOT: PL011 RW OK");

    astia_aarch64_install_exception_vectors();
    console.write_line("ASTIA AARCH64 VIRT BOOT: EXCEPTION VECTORS");
    write_line_field(
        console, "exception: vbar=", reinterpret_cast<astia::u64>(__vector_table), 16);

    write_field(console, "gic: gicd=", info.gic.distributor_base, 16);
    write_field(console, " size=", info.gic.distributor_size, 8);
    console.write_line("");
    write_field(console, "gic: gicr=", info.gic.redistributor_base, 16);
    write_field(console, " size=", info.gic.redistributor_size, 8);
    console.write_line("");

    const gicv3::boot_config gic_config{
        {info.gic.distributor_base, info.gic.distributor_size},
        {info.gic.redistributor_base, info.gic.redistributor_size}};
    gicv3::init_result gic{};
    const gicv3::error gic_error = gicv3::initialize(gic_config, gic);
    if (gic_error != gicv3::error::none) {
        console.write("gic: error=");
        console.write_line(gicv3::describe(gic_error));
        fail(console, "GICv3 initialization failed");
    }

    write_field(console, "gic: redistributor=", gic.redistributor_base, 16);
    write_field(console, " affinity=", gic.affinity, 8);
    console.write_line("");
    write_line_field(console, "gic: gicd-ctlr=", gic.distributor_ctlr, 8);
    console.write("gic: sre=");
    console.write_hex(gic.sre & 1, 1);
    console.write(" dib=");
    console.write_hex((gic.sre >> 2) & 1, 1);
    console.write(" dfb=");
    console.write_hex((gic.sre >> 1) & 1, 1);
    console.write(" pmr=0x");
    console.write_hex(gic.pmr & 0xff, 2);
    console.write(" bpr1=0x");
    console.write_hex(gic.bpr1 & 7, 1);
    console.write(" eoimode=");
    console.write_hex((gic.ctlr_el1 >> 1) & 1, 1);
    console.write(" igrpen1=");
    console.write_hex(gic.igrpen1 & 1, 1);
    console.write_line("");
    write_line_field(console, "gic: self-sgi=", gicv3::self_sgi_id, 8);
    console.write_line("ASTIA AARCH64 VIRT BOOT: GIC READY");

    console.write_line("ASTIA AARCH64 VIRT BOOT: IRQ ENABLED");
    __asm__ volatile("msr daifclr, #2" : : : "memory");
    console.write_line("ASTIA AARCH64 VIRT BOOT: IRQ SELF-SGI");
    gicv3::trigger_self_sgi();

    constexpr astia::u32 self_sgi_spin_limit = 10000000;
    astia::u32           spin                = 0;
    while (gicv3::self_sgi_count() == 0 && spin < self_sgi_spin_limit) {
        __asm__ volatile("yield" : : : "memory");
        ++spin;
    }
    __asm__ volatile("msr daifset, #2" : : : "memory");
    if (gicv3::self_sgi_count() != 1) {
        fail(console, "self-SGI was not handled");
    }
    console.write_line("ASTIA AARCH64 VIRT BOOT: IRQ RETURNED");
    console.write_line("ASTIA AARCH64 VIRT BOOT: GIC SELF-SGI OK");

    write_line_field(console, "timer: ppi=", timer::ppi_id, 8);
    timer::arm_result  armed{};
    const timer::error timer_error = timer::arm_one_shot(armed);
    if (timer_error != timer::error::none) {
        console.write("timer: error=");
        console.write_line(timer::describe(timer_error));
        fail(console, "EL1 physical timer arm failed");
    }
    write_field(console, "timer: frequency=", armed.frequency_hz, 8);
    write_line_field(console, " ticks=", armed.ticks, 8);
    console.write("timer: ctl=");
    console.write("0x");
    console.write_hex(armed.control, 8);
    write_bit(console, " enable=", armed.control, 0);
    write_bit(console, " imask=", armed.control, 1);
    write_bit(console, " istatus=", armed.control, 2);
    console.write_line("");
    console.write_line("ASTIA AARCH64 VIRT BOOT: TIMER ARMED");

    __asm__ volatile("msr daifclr, #2" : : : "memory");
    __asm__ volatile("isb" : : : "memory");
    constexpr astia::u32 timer_spin_limit = 100000000;
    astia::u32           timer_spin       = 0;
    while (gicv3::timer_count() == 0 && timer_spin < timer_spin_limit) {
        __asm__ volatile("yield" : : : "memory");
        ++timer_spin;
    }
    __asm__ volatile("msr daifset, #2" : : : "memory");
    __asm__ volatile("isb" : : : "memory");
    if (gicv3::timer_count() != 1) {
        fail(console, "timer PPI was not handled exactly once");
    }
    console.write_line("ASTIA AARCH64 VIRT BOOT: TIMER RETURNED");
    console.write_line("ASTIA AARCH64 VIRT BOOT: TIMER PPI OK");

    constexpr astia::u64 brk_far_sentinel = 0x5a5a5a5a5a5a5a5aULL;
    write_line_field(console, "exception: far-seed=", brk_far_sentinel, 16);
    astia_aarch64_seed_far_el1(brk_far_sentinel);
    astia_aarch64_brk_probe();
}
