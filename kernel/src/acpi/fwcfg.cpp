#include "fwcfg.hpp"

#include <astia/arch/io.hpp>

#include "io.hpp"

namespace astia::acpi::detail {
    namespace {
        constexpr u16 selector_port      = 0x510;
        constexpr u16 data_port          = 0x511;
        constexpr u16 selector_signature = 0x0000;
        constexpr u16 selector_file_dir  = 0x0019;

        struct file_entry {
            u32  size   = 0;
            u16  select = 0;
            char name[56]{};
        };

        bool same_name(const char* lhs, const char* rhs) {
            for (u32 index = 0; index < 56; ++index) {
                if (lhs[index] != rhs[index])
                    return false;

                if (lhs[index] == '\0')
                    return true;
            }

            return true;
        }

        u32 read_be32() {
            u32 value = 0;

            for (u32 index = 0; index < 4; ++index) {
                value = (value << 8) | astia::arch::pio::read8(data_port);
            }

            return value;
        }

        u16 read_be16() {
            const u8 high = astia::arch::pio::read8(data_port);
            const u8 low  = astia::arch::pio::read8(data_port);

            return static_cast<u16>((static_cast<u16>(high) << 8) | low);
        }

        bool available() {
            astia::arch::pio::write16(selector_port, selector_signature);

            const char expected[] = {'Q', 'E', 'M', 'U'};

            for (char value : expected) {
                if (astia::arch::pio::read8(data_port) != static_cast<u8>(value))
                    return false;
            }

            return true;
        }

        bool find(const char* name, file_entry& out) {
            if (!available())
                return false;

            astia::arch::pio::write16(selector_port, selector_file_dir);

            const u32 count = read_be32();

            if (count == 0 || count > 1024)
                return false;

            for (u32 index = 0; index < count; ++index) {
                file_entry entry{};

                entry.size   = read_be32();
                entry.select = read_be16();

                (void) read_be16(); // reserved

                for (char& byte : entry.name) {
                    byte = static_cast<char>(astia::arch::pio::read8(data_port));
                }

                if (same_name(entry.name, name)) {
                    out = entry;

                    return true;
                }
            }

            return false;
        }
    } // namespace

    bool fw_cfg_size(const char* name, u32& size) {
        file_entry entry{};

        if (name == nullptr || !find(name, entry) || entry.size == 0)
            return false;

        size = entry.size;

        return true;
    }

    bool fw_cfg_read(const char* name, u8* destination, u32 capacity) {
        file_entry entry{};

        if (name == nullptr || destination == nullptr || !find(name, entry) ||
            entry.size > capacity)
            return false;

        astia::arch::pio::write16(selector_port, entry.select);

        for (u32 index = 0; index < entry.size; ++index) {
            destination[index] = astia::arch::pio::read8(data_port);
        }

        return true;
    }
} // namespace astia::acpi::detail
