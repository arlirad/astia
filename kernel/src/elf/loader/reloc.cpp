#include "internal.hpp"

namespace astia::elf::loader {
    using namespace detail;

    namespace {
        bool str_eq(const char* a, const char* b) {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }

            return *a == *b;
        }

        bool array_priority(const char* name, usize name_size, const char* base,
                            u16& priority) {
            usize i = 0;

            while (base[i] != '\0') {
                if (i == name_size || name[i] != base[i])
                    return false;

                ++i;
            }

            if (i == name_size) {
                priority = 65535;

                return true;
            }

            if (name[i] != '.' || i + 1 == name_size)
                return false;

            ++i;

            u32 value = 0;

            for (; i != name_size; ++i) {
                if (name[i] < '0' || name[i] > '9')
                    return false;

                const u32 digit = static_cast<u32>(name[i] - '0');

                if (value > (65535u - digit) / 10u)
                    return false;

                value = value * 10u + digit;
            }

            priority = static_cast<u16>(value);

            return true;
        }

        struct array_section {
            const u8* data;
            usize     count;
            u16       index;
            u16       priority;
        };

        void sort_array_sections(array_section* sections, usize count) {
            for (usize i = 1; i < count; ++i) {
                const array_section current = sections[i];

                usize j = i;

                while (j != 0 && (current.priority < sections[j - 1].priority ||
                                  (current.priority == sections[j - 1].priority &&
                                   current.index < sections[j - 1].index))) {
                    sections[j] = sections[j - 1];

                    --j;
                }

                sections[j] = current;
            }
        }
    } // namespace

    // ========================================================================
    //  Relocatable (ET_REL) loader
    // ========================================================================

    bool load_relocatable(span<const u8> bytes, const env& e, image& out) {
        const usize len = bytes.size();

        if (e.map == nullptr || e.unmap == nullptr)
            return false;

        out = {};

        view v;

        if (!parse_relocatable(bytes, v))
            return false;

        // --- Pass 1: compute a contiguous layout for the SHF_ALLOC sections.
        // Keeping every section in one block guarantees internal PC32
        // relocations stay within 32-bit range, and gives us a single
        // allocation to hand back for later unload.
        void* sec_base_storage[max_sections] = {};

        u64  offset    = 0;
        u64  max_align = 1;
        bool any_alloc = false;

        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& sh = v.sh[i];

            if ((sh.sh_flags & shf_alloc) == 0)
                continue;

            const u64 align = sh.sh_addralign == 0 ? 1 : sh.sh_addralign;

            if (!is_power_of_two(align))
                return false;

            if (align > max_align)
                max_align = align;

            u64 aligned = 0;

            if (!checked_align_up(offset, align, aligned) ||
                !checked_add(aligned, sh.sh_size, offset))
                return false;

            any_alloc = true;
        }

        if (!any_alloc || offset == 0)
            return false;

        if (offset > static_cast<u64>(~usize{0}))
            return false;

        void* block = e.map(e.ctx, 0, static_cast<usize>(offset),
                            static_cast<usize>(max_align), false);
        if (block == nullptr)
            return false;

        struct mapped_block {
            const env& environment;
            void*      address;
            usize      size;

            ~mapped_block() {
                if (address != nullptr)
                    environment.unmap(environment.ctx, address, size);
            }

            void keep() {
                address = nullptr;
            }
        } mapped{e, block, static_cast<usize>(offset)};

        // Zero the whole block up front so .bss (SHT_NOBITS) and any padding
        // are already cleared without a separate pass.
        {
            u8* p = static_cast<u8*>(block);

            for (u64 i = 0; i < offset; ++i) {
                p[i] = 0;
            }
        }

        // --- Pass 2: place each allocated section and copy its contents.
        offset = 0;

        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& sh = v.sh[i];

            if ((sh.sh_flags & shf_alloc) == 0) {
                sec_base_storage[i] = nullptr;

                continue;
            }

            const u64 align   = sh.sh_addralign == 0 ? 1 : sh.sh_addralign;
            u64       aligned = 0;

            if (!checked_align_up(offset, align, aligned))
                return false;

            offset = aligned;

            u8* dst = static_cast<u8*>(block) + offset;

            sec_base_storage[i] = dst;

            // SHT_NOBITS (.bss) occupies memory but has no file bytes; the
            // block was zeroed above, so nothing to copy.
            if (sh.sh_type != sht_nobits) {
                if (!in_bounds(sh.sh_offset, sh.sh_size, len))
                    return false;

                const u8* src = v.base + sh.sh_offset;

                for (u64 k = 0; k < sh.sh_size; ++k) {
                    dst[k] = src[k];
                }
            }

            if (!checked_add(offset, sh.sh_size, offset))
                return false;
        }

        const void* const* sec_base = sec_base_storage;

        // --- Apply relocations. Each SHT_RELA section names the section it
        // patches (sh_info) and the symbol table it uses (sh_link).
        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& sh = v.sh[i];

            if (sh.sh_type != sht_rela)
                continue;

            if (sh.sh_info >= v.shnum)
                return false;

            if (sh.sh_link != v.sym_index || sh.sh_entsize != sizeof(elf64_rela) ||
                sh.sh_size % sizeof(elf64_rela) != 0 ||
                !in_bounds(sh.sh_offset, sh.sh_size, len))
                return false;

            // A relocation against a section we did not load (.comment,
            // .debug_*, ...) patches data that is not in memory; skip it.
            if (sec_base[sh.sh_info] == nullptr)
                continue;

            u8* target = static_cast<u8*>(sec_base_storage[sh.sh_info]);

            u64   target_size = v.sh[sh.sh_info].sh_size;
            usize count       = sh.sh_size / sizeof(elf64_rela);

            const elf64_rela* rela =
                reinterpret_cast<const elf64_rela*>(v.base + sh.sh_offset);

            for (usize k = 0; k < count; ++k) {
                const elf64_rela& r = rela[k];

                u32 type = r_type(r.r_info);
                if (type == r_x86_64_none)
                    continue;

                // The widest store below is 8 bytes; make sure the slot lies
                // inside the section it patches.
                u64 wsize = (type == r_x86_64_64) ? 8 : 4;

                if (r.r_offset > target_size || wsize > target_size - r.r_offset)
                    return false;

                u64 s = 0;

                if (!sym_addr(v, e, sec_base, r_sym(r.r_info), s))
                    return false;

                u64 p = reinterpret_cast<u64>(target) + r.r_offset;
                i64 a = r.r_addend;

                switch (type) {
                case r_x86_64_64: {
                    write_u64(reinterpret_cast<u8*>(p), s + static_cast<u64>(a));
                    break;
                }
                case r_x86_64_pc32:
                case r_x86_64_plt32: {
                    i64 rel = static_cast<i64>(s) + a - static_cast<i64>(p);

                    if (rel < -2147483648LL || rel > 2147483647LL) {
                        return false; // does not fit signed 32-bit
                    }

                    write_u32(reinterpret_cast<u8*>(p), static_cast<u32>(rel));
                    break;
                }
                case r_x86_64_32: {
                    u64 val = s + static_cast<u64>(a);
                    if (val > 0xffffffffull)
                        return false;

                    write_u32(reinterpret_cast<u8*>(p), static_cast<u32>(val));
                    break;
                }
                case r_x86_64_32s: {
                    i64 val = static_cast<i64>(s) + a;

                    if (val < -2147483648LL || val > 2147483647LL)
                        return false;

                    write_u32(reinterpret_cast<u8*>(p), static_cast<u32>(val));
                    break;
                }
                default:
                    return false; // unsupported relocation type
                }
            }
        }

        constexpr const char* module_entry_symbol = "_Z11module_mainv";

        u32 entry_index = ~u32{0};

        for (usize i = 0; i < v.symcount; ++i) {
            const elf64_sym& sym = v.sym[i];

            const char* name = nullptr;

            if (!symbol_name(v, sym, name))
                return false;

            if (!str_eq(name, module_entry_symbol))
                continue;

            if (entry_index != ~u32{0} || sym.st_shndx == shn_undef ||
                sym.st_shndx >= v.shnum || st_type(sym.st_info) != stt_func ||
                st_bind(sym.st_info) != stb_global || st_visibility(sym.st_other) != 0 ||
                (v.sh[sym.st_shndx].sh_flags & (shf_alloc | shf_execinstr)) !=
                    (shf_alloc | shf_execinstr) ||
                sym.st_value >= v.sh[sym.st_shndx].sh_size ||
                (sym.st_size != 0 &&
                 sym.st_size > v.sh[sym.st_shndx].sh_size - sym.st_value))
                return false;

            entry_index = static_cast<u32>(i);
        }

        if (entry_index == ~u32{0})
            return false;

        auto executable_address = [&](u64 address) {
            for (u16 i = 0; i < v.shnum; ++i) {
                const elf64_shdr& section = v.sh[i];

                if ((section.sh_flags & (shf_alloc | shf_execinstr)) !=
                        (shf_alloc | shf_execinstr) ||
                    sec_base[i] == nullptr)
                    continue;

                const u64 base = reinterpret_cast<u64>(sec_base[i]);

                if (address >= base && address - base < section.sh_size)
                    return true;
            }

            return false;
        };

        array_section init_sections[max_init_array_entries] = {};
        array_section fini_sections[max_init_array_entries] = {};

        usize init_section_count = 0;
        usize fini_section_count = 0;
        usize init_entry_count   = 0;
        usize fini_entry_count   = 0;

        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& section = v.sh[i];

            const char* name = nullptr;

            usize name_size = 0;

            if (!section_name(v, i, name, name_size))
                return false;

            if (str_eq(name, ".preinit_array") || str_eq(name, ".init") ||
                str_eq(name, ".fini") || str_eq(name, ".ctors") || str_eq(name, ".dtors"))
                return false;

            if (section.sh_type != sht_init_array && section.sh_type != sht_fini_array)
                continue;

            if ((section.sh_flags & (shf_alloc | shf_write)) != (shf_alloc | shf_write) ||
                (section.sh_entsize != 0 && section.sh_entsize != sizeof(u64)) ||
                section.sh_size % sizeof(u64) != 0)
                return false;

            const usize count    = section.sh_size / sizeof(u64);
            u16         priority = 0;

            const char* base =
                section.sh_type == sht_init_array ? ".init_array" : ".fini_array";

            if (!array_priority(name, name_size, base, priority))
                return false;

            if (count == 0)
                continue;

            array_section* sections =
                section.sh_type == sht_init_array ? init_sections : fini_sections;
            usize* section_count = section.sh_type == sht_init_array
                                       ? &init_section_count
                                       : &fini_section_count;
            usize* entry_count =
                section.sh_type == sht_init_array ? &init_entry_count : &fini_entry_count;

            if (*section_count == max_init_array_entries ||
                count > max_init_array_entries - *entry_count)
                return false;

            sections[*section_count] = {static_cast<const u8*>(sec_base[i]), count, i,
                                        priority};

            ++*section_count;

            *entry_count += count;
        }

        sort_array_sections(init_sections, init_section_count);
        sort_array_sections(fini_sections, fini_section_count);

        const auto append_array = [&](const array_section* sections, usize section_count,
                                      init_fn* destination, usize& destination_count) {
            for (usize i = 0; i < section_count; ++i) {
                const array_section& section = sections[i];

                for (usize j = 0; j < section.count; ++j) {
                    const u64 address = read_u64_bytes(section.data + j * sizeof(u64));

                    if (address != 0 && !executable_address(address))
                        return false;

                    destination[destination_count++] = reinterpret_cast<init_fn>(address);
                }
            }

            return true;
        };

        if (!append_array(init_sections, init_section_count, out.init_array,
                          out.init_count) ||
            !append_array(fini_sections, fini_section_count, out.fini_array,
                          out.fini_count))
            return false;

        u64 addr = 0;

        if (!sym_addr(v, e, sec_base, entry_index, addr) || !executable_address(addr))
            return false;

        out.entry     = reinterpret_cast<void*>(addr);
        out.base      = block;
        out.size      = static_cast<usize>(offset);
        out.loaded    = kind::relocatable;
        out.load_bias = 0;
        out.nsegs     = 0;

        mapped.keep();

        return true;
    }
} // namespace astia::elf::loader
