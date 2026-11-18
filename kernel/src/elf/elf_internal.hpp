#pragma once

#include <astia/elf.hpp>

namespace astia::elf::detail {

    using u8  = astia::u8;
    using u16 = astia::u16;
    using u32 = astia::u32;
    using u64 = astia::u64;
    using i64 = astia::i64;

    inline constexpr usize ei_nident    = elf_ident_size;
    inline constexpr u32   ei_class     = 4;
    inline constexpr u32   ei_data      = 5;
    inline constexpr u8    elfclass64   = 2;
    inline constexpr u8    elfdatal2lsb = 1;

    inline constexpr u16 et_rel    = 1;
    inline constexpr u16 et_exec   = 2;
    inline constexpr u16 et_dyn    = 3;
    inline constexpr u16 em_x86_64 = 62;

    inline constexpr u32 sht_symtab = 2;
    inline constexpr u32 sht_strtab = 3;
    inline constexpr u32 sht_rela   = 4;
    inline constexpr u32 sht_nobits = 8;

    inline constexpr u64 shf_alloc = 1ull << 1;

    inline constexpr u16 shn_undef  = 0;
    inline constexpr u16 shn_abs    = 0xfff1;
    inline constexpr u16 shn_common = 0xfff2;

    inline constexpr u32 pt_load    = 1;
    inline constexpr u32 pt_dynamic = 2;
    inline constexpr u32 pt_tls     = 7;

    inline constexpr i64 dt_null    = 0;
    inline constexpr i64 dt_rela    = 7;
    inline constexpr i64 dt_relasz  = 8;
    inline constexpr i64 dt_relaent = 9;

    inline constexpr u32 r_x86_64_none     = 0;
    inline constexpr u32 r_x86_64_64       = 1;
    inline constexpr u32 r_x86_64_pc32     = 2;
    inline constexpr u32 r_x86_64_plt32    = 4;
    inline constexpr u32 r_x86_64_relative = 8;
    inline constexpr u32 r_x86_64_32       = 10;
    inline constexpr u32 r_x86_64_32s      = 11;

    struct elf64_ehdr {
        u8  e_ident[ei_nident];
        u16 e_type;
        u16 e_machine;
        u32 e_version;
        u64 e_entry;
        u64 e_phoff;
        u64 e_shoff;
        u32 e_flags;
        u16 e_ehsize;
        u16 e_phentsize;
        u16 e_phnum;
        u16 e_shentsize;
        u16 e_shnum;
        u16 e_shstrndx;
    };

    struct elf64_phdr {
        u32 p_type;
        u32 p_flags;
        u64 p_offset;
        u64 p_vaddr;
        u64 p_paddr;
        u64 p_filesz;
        u64 p_memsz;
        u64 p_align;
    };

    struct elf64_shdr {
        u32 sh_name;
        u32 sh_type;
        u64 sh_flags;
        u64 sh_addr;
        u64 sh_offset;
        u64 sh_size;
        u32 sh_link;
        u32 sh_info;
        u64 sh_addralign;
        u64 sh_entsize;
    };

    struct elf64_sym {
        u32 st_name;
        u8  st_info;
        u8  st_other;
        u16 st_shndx;
        u64 st_value;
        u64 st_size;
    };

    struct elf64_rela {
        u64 r_offset;
        u64 r_info;
        i64 r_addend;
    };

    struct elf64_dyn {
        i64 d_tag;
        u64 d_val;
    };

    static_assert(sizeof(elf64_ehdr) == 64, "Elf64_Ehdr layout");
    static_assert(sizeof(elf64_phdr) == 56, "Elf64_Phdr layout");
    static_assert(sizeof(elf64_shdr) == 64, "Elf64_Shdr layout");
    static_assert(sizeof(elf64_sym) == 24, "Elf64_Sym layout");
    static_assert(sizeof(elf64_rela) == 24, "Elf64_Rela layout");
    static_assert(sizeof(elf64_dyn) == 16, "Elf64_Dyn layout");

    constexpr u8 st_bind(u8 info) {
        return info >> 4;
    }

    constexpr u32 r_sym(u64 info) {
        return static_cast<u32>(info >> 32);
    }

    constexpr u32 r_type(u64 info) {
        return static_cast<u32>(info & 0xffffffffu);
    }

    inline constexpr u8 stb_weak = 2;

    inline bool is_power_of_two(u64 value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    inline bool checked_add(u64 left, u64 right, u64& result) {
        if (left > ~u64{0} - right)
            return false;
        result = left + right;
        return true;
    }

    inline bool checked_align_up(u64 value, u64 alignment, u64& result) {
        if (!is_power_of_two(alignment) || value > ~u64{0} - (alignment - 1))
            return false;
        result = (value + alignment - 1) & ~(alignment - 1);
        return true;
    }

    inline bool checked_biased_value(u64 mapped_addr, u64 span_start, i64 addend,
                                     u64& result) {
        if (mapped_addr >= span_start) {
            const u64 bias = mapped_addr - span_start;
            if (addend >= 0)
                return checked_add(bias, static_cast<u64>(addend), result);
            const u64 magnitude = static_cast<u64>(-(addend + 1)) + 1;
            if (bias < magnitude)
                return false;
            result = bias - magnitude;
            return true;
        }
        const u64 magnitude = span_start - mapped_addr;
        if (addend >= 0) {
            const u64 value = static_cast<u64>(addend);
            if (value >= magnitude) {
                result = value - magnitude;
                return true;
            }
            result = 0;
            return false;
        }
        const u64 addend_magnitude = static_cast<u64>(-(addend + 1)) + 1;
        if (magnitude > ~u64{0} - addend_magnitude)
            return false;
        const u64 total = magnitude + addend_magnitude;
        result = 0;
        return total == 0;
    }

    inline bool in_bounds(u64 off, u64 size, usize len) {
        return off <= len && size <= len - off;
    }

    inline void write_u32(u8* destination, u32 value) {
        for (usize i = 0; i < 4; ++i)
            destination[i] = static_cast<u8>(value >> (i * 8));
    }

    inline void write_u64(u8* destination, u64 value) {
        for (usize i = 0; i < 8; ++i)
            destination[i] = static_cast<u8>(value >> (i * 8));
    }

    inline u64 read_u64_bytes(const u8* source) {
        u64 value = 0;
        for (usize i = 0; i < 8; ++i)
            value |= static_cast<u64>(source[i]) << (i * 8);
        return value;
    }

    constexpr u64 align_down(u64 value) {
        return value & ~(page_size - 1);
    }

    constexpr u64 align_up(u64 value) {
        return (value + page_size - 1) & ~(page_size - 1);
    }

    inline bool ident_type(span<const u8> bytes, u16& type_out) {
        if (bytes.data() == nullptr || bytes.size() < sizeof(elf64_ehdr))
            return false;
        const auto* eh = reinterpret_cast<const elf64_ehdr*>(bytes.data());
        if (eh->e_ident[0] != elf_magic[0] || eh->e_ident[1] != elf_magic[1] ||
            eh->e_ident[2] != elf_magic[2] || eh->e_ident[3] != elf_magic[3])
            return false;
        if (eh->e_ident[ei_class] != elfclass64 ||
            eh->e_ident[ei_data] != elfdatal2lsb || eh->e_machine != em_x86_64)
            return false;
        type_out = eh->e_type;
        return true;
    }

    struct view {
        const u8*         base     = nullptr;
        usize             len      = 0;
        const elf64_ehdr* eh       = nullptr;
        const elf64_shdr* sh       = nullptr;
        u16               shnum    = 0;
        u16               sym_index = 0;
        const elf64_sym*  sym      = nullptr;
        usize             symcount = 0;
        const char*       str      = nullptr;
        usize             strsize  = 0;
    };

    bool symbol_name(const view& v, const elf64_sym& sym, const char*& out);
    bool parse_relocatable(span<const u8> bytes, view& v);
    bool sym_addr(const view& v, const env& e, const void* const* sec_base, u32 index,
                  u64& out);

} // namespace astia::elf::detail
