#include "pl011.hpp"

namespace astia::plat::aarch64_virt {
    namespace {
        constexpr u32 data_register  = 0x00;
        constexpr u32 flags_register = 0x18;
        constexpr u32 transmit_full  = 1u << 5;
        constexpr u32 max_hex_digits = 16;

        constexpr char hex_digit(u32 value) {
            return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
        }
    } // namespace

    void pl011_uart::write_char(char value) const {
        auto* flags = reinterpret_cast<volatile u32*>(base_ + flags_register);

        while ((*flags & transmit_full) != 0) {
        }

        auto* data = reinterpret_cast<volatile u32*>(base_ + data_register);

        *data = static_cast<u32>(static_cast<u8>(value));
    }

    void pl011_uart::write(const char* text) const {
        while (*text != '\0') {
            write_char(*text++);
        }
    }

    void pl011_uart::write_line(const char* text) const {
        write(text);
        write_char('\n');
    }

    void pl011_uart::write_hex(u64 value, u32 digits) const {
        if (digits > max_hex_digits)
            digits = max_hex_digits;

        for (u32 index = 0; index < digits; ++index) {
            const u32 shift = (digits - 1 - index) * 4;

            write_char(hex_digit(static_cast<u32>((value >> shift) & 0xfULL)));
        }
    }

    u32 pl011_uart::read_flags() const {
        const auto* flags = reinterpret_cast<volatile const u32*>(base_ + flags_register);

        return *flags;
    }
} // namespace astia::plat::aarch64_virt
