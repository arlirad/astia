#include <astia/boot/aarch64/fdt.hpp>
#include <astia/boot/aarch64/virt.hpp>

#include "board.hpp"
#include "pl011.hpp"
#include "psci.hpp"

namespace {
    using astia::boot::aarch64::virt::boot_info;
    using astia::boot::aarch64::virt::console_source;
    using astia::plat::aarch64_virt::pl011_uart;

    [[nodiscard]] astia::u64 current_exception_level() {
        astia::u64 value = 0;
        __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
        return (value >> 2) & 0x3ULL;
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
    console.write_line("ASTIA AARCH64 VIRT BOOT: HALT");
    psci::system_off();
}
