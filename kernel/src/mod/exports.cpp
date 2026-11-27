#include "exports.hpp"

namespace astia::mod {
    namespace {
        constexpr u32   table_magic   = 0x41535453;
        constexpr u16   table_version = 1;
        constexpr usize header_size   = 20;
        constexpr usize entry_size    = 16;

        extern "C" const u8 __kernel_symbol_table_start[] __attribute__((weak));
        extern "C" const u8 __kernel_symbol_table_end[] __attribute__((weak));

        u16 read_u16(const u8* p) {
            return static_cast<u16>(p[0]) | (static_cast<u16>(p[1]) << 8);
        }

        u32 read_u32(const u8* p) {
            return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
                   (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
        }

        u64 read_u64(const u8* p) {
            u64 value = 0;

            for (usize i = 0; i < 8; ++i) {
                value |= static_cast<u64>(p[i]) << (i * 8);
            }

            return value;
        }

        bool name_eq(const u8* table_name, const char* name, usize limit) {
            usize i = 0;

            while (i < limit && table_name[i] != 0 && name[i] != 0) {
                if (table_name[i] != static_cast<u8>(name[i]))
                    return false;

                ++i;
            }

            return i < limit && table_name[i] == 0 && name[i] == 0;
        }
    } // namespace

    u64 resolve(const char* name) {
        const u8* start = __kernel_symbol_table_start;
        const u8* end   = __kernel_symbol_table_end;

        if (name == nullptr || start == nullptr || end == nullptr || end <= start)
            return 0;

        const u8* table = start;

        const usize size = static_cast<usize>(end - table);

        if (size < header_size || read_u32(table) != table_magic ||
            read_u16(table + 4) != table_version || read_u16(table + 6) != entry_size)
            return 0;

        const u32 count          = read_u32(table + 8);
        const u32 strings_offset = read_u32(table + 12);
        const u32 total_size     = read_u32(table + 16);

        if (total_size > size || strings_offset < header_size ||
            strings_offset > total_size ||
            count > (strings_offset - header_size) / entry_size)
            return 0;

        const u8* strings = table + strings_offset;

        const usize strings_size = total_size - strings_offset;

        for (u32 i = 0; i < count; ++i) {
            const u8* entry = table + header_size + static_cast<usize>(i) * entry_size;

            const u32 name_offset = read_u32(entry + 8);
            if (name_offset >= strings_size)
                continue;

            if (name_eq(strings + name_offset, name, strings_size - name_offset))
                return read_u64(entry);
        }

        return 0;
    }
} // namespace astia::mod
