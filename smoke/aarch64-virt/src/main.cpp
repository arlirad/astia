#include "board.hpp"
#include "pl011.hpp"
#include "psci.hpp"
#include "types.hpp"

namespace astia::smoke {
    namespace {
        constexpr u32 dtb_magic        = 0xd00dfeedU;
        constexpr u32 dtb_minimum_size = 0x100U;

        [[nodiscard]] u32 load_be32(const volatile u8* bytes) {
            return (static_cast<u32>(bytes[0]) << 24) |
                   (static_cast<u32>(bytes[1]) << 16) |
                   (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
        }

        [[nodiscard]] u64 current_el() {
            u64 value = 0;
            __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
            return (value >> 2) & 0x3ULL;
        }

        [[nodiscard]] u64 current_daif() {
            u64 value = 0;
            __asm__ volatile("mrs %0, daif" : "=r"(value));
            return value;
        }

        [[nodiscard]] constexpr bool dtb_totalsize_valid(u32 total_size) {
            return total_size >= dtb_minimum_size &&
                   total_size <= qemu_virt_dtb_window_size;
        }

        void write_field(const pl011_uart& console, const char* name, u64 value,
                         u32 digits) {
            console.write(name);
            console.write("0x");
            console.write_hex(value, digits);
        }
    } // namespace

    extern "C" [[noreturn]] void astia_smoke_main(u64 entry_x0, u64 entry_sp) {
        const pl011_uart console{qemu_virt_pl011_base};

        console.write_line("");
        console.write_line("ASTIA AARCH64 VIRT SMOKE: BOOT OK");

        write_field(console, "entry-x0: ", entry_x0, 16);
        console.write_line("");
        write_field(console, "entry-sp: ", entry_sp, 16);
        console.write_line("");
        write_field(console, "current-el: ", current_el(), 1);
        console.write_line("");
        write_field(console, "entry-daif: ", current_daif(), 3);
        console.write_line("");

        const auto* dtb   = reinterpret_cast<const volatile u8*>(qemu_virt_dtb_address);
        const u32   magic = load_be32(dtb);
        const u32   total_size = load_be32(dtb + 4);
        const bool  valid      = magic == dtb_magic && dtb_totalsize_valid(total_size);

        write_field(console, "dtb: address=", qemu_virt_dtb_address, 8);
        write_field(console, " magic=", magic, 8);
        write_field(console, " size=", total_size, 8);
        console.write(valid ? " valid=yes" : " valid=no");
        console.write_line("");

        if (valid) {
            console.write_line("ASTIA AARCH64 VIRT SMOKE: PASS");
        }
        else {
            console.write_line("ASTIA AARCH64 VIRT SMOKE: FAIL");
        }

        psci::system_off();
    }
} // namespace astia::smoke
