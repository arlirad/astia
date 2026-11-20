// x86-64 ELF format parsing and validation.
//
// Structure layouts and constants below are taken verbatim from the System V
// ABI x86-64 supplement (mirrored by glibc's <elf.h>). They are redeclared
// locally so the kernel build stays freestanding and the host test does not
// depend on a system header.

#include <astia/strings.hpp>

#include "elf_internal.hpp"

namespace astia::elf {

    using namespace detail;

    namespace detail {

        // ====================================================================
        //  Relocatable (ET_REL) path
        // ====================================================================

        bool symbol_name(const view& v, const elf64_sym& sym, const char*& out) {
            if (sym.st_name >= v.strsize)
                return false;
            const char* name = v.str + sym.st_name;
            string_view name_view;
            if (!string_before_nul({name, v.strsize - sym.st_name}, name_view))
                return false;
            out = name_view.data;
            return true;
        }

        bool section_name(const view& v, u16 index, const char*& out, usize& out_size) {
            if (index >= v.shnum || v.shstr == nullptr)
                return false;
            const u32 offset = v.sh[index].sh_name;
            if (offset >= v.shstrsize)
                return false;
            const char* name = v.shstr + offset;
            string_view name_view;
            if (!string_before_nul({name, v.shstrsize - offset}, name_view))
                return false;
            out      = name_view.data;
            out_size = name_view.size;
            return true;
        }

        // Validate the section table, then locate the symbol table and its
        // linked string table. Returns false on anything malformed or
        // truncated.
        bool parse_relocatable(span<const u8> bytes, view& v) {
            const usize len  = bytes.size();
            u16         type = 0;
            if (!ident_type(bytes, type) || type != et_rel) {
                return false;
            }
            v.base = bytes.data();
            v.len  = bytes.size();
            v.eh   = reinterpret_cast<const elf64_ehdr*>(v.base);

            const elf64_ehdr& eh = *v.eh;
            if (eh.e_shnum == 0 || eh.e_shnum > max_sections) {
                return false;
            }
            if (eh.e_shentsize != sizeof(elf64_shdr)) {
                return false;
            }
            if (!in_bounds(eh.e_shoff, static_cast<u64>(eh.e_shnum) * sizeof(elf64_shdr),
                           len)) {
                return false;
            }

            v.sh    = reinterpret_cast<const elf64_shdr*>(v.base + eh.e_shoff);
            v.shnum = eh.e_shnum;
            if (eh.e_shstrndx >= v.shnum)
                return false;
            const elf64_shdr& shstr = v.sh[eh.e_shstrndx];
            if (shstr.sh_type != sht_strtab ||
                !in_bounds(shstr.sh_offset, shstr.sh_size, len))
                return false;
            v.shstr     = reinterpret_cast<const char*>(v.base + shstr.sh_offset);
            v.shstrsize = static_cast<usize>(shstr.sh_size);

            // Locate SHT_SYMTAB and follow sh_link to its SHT_STRTAB.
            for (u16 i = 0; i < v.shnum; ++i) {
                const elf64_shdr& sh = v.sh[i];
                if (i != 0 && sh.sh_addralign != 0 && !is_power_of_two(sh.sh_addralign))
                    return false;
                if (sh.sh_type != sht_symtab) {
                    continue;
                }
                if (sh.sh_entsize != sizeof(elf64_sym)) {
                    return false;
                }
                if (sh.sh_size % sizeof(elf64_sym) != 0) {
                    return false;
                }
                if (!in_bounds(sh.sh_offset, sh.sh_size, len)) {
                    return false;
                }
                if (sh.sh_link >= v.shnum) {
                    return false;
                }
                const elf64_shdr& str = v.sh[sh.sh_link];
                if (str.sh_type != sht_strtab ||
                    !in_bounds(str.sh_offset, str.sh_size, len)) {
                    return false;
                }
                v.sym       = reinterpret_cast<const elf64_sym*>(v.base + sh.sh_offset);
                v.symcount  = sh.sh_size / sizeof(elf64_sym);
                v.str       = reinterpret_cast<const char*>(v.base + str.sh_offset);
                v.strsize   = static_cast<usize>(str.sh_size);
                v.sym_index = i;
                for (usize j = 0; j < v.symcount; ++j) {
                    const char* name = nullptr;
                    if (!symbol_name(v, v.sym[j], name))
                        return false;
                }
                return true;
            }
            // No symbol table: nothing to relocate or enter.
            return false;
        }

        // Resolve a symbol to its runtime address. `sec_base` holds the load
        // address of each allocated section (null for non-allocated ones).
        // Defined symbols fold in their section base; undefined globals go
        // through the caller's resolver.
        bool sym_addr(const view& v, const env& e, const void* const* sec_base, u32 index,
                      u64& out) {
            if (index >= v.symcount) {
                return false;
            }
            const elf64_sym& sym = v.sym[index];

            if (sym.st_shndx == shn_undef) {
                const char* name = nullptr;
                if (!symbol_name(v, sym, name))
                    return false;
                u64 resolved = e.resolve != nullptr ? e.resolve(e.ctx, name) : 0;
                if (resolved == 0) {
                    // A weak undefined symbol is allowed to be null; anything
                    // else must resolve or the image is unusable.
                    if (st_bind(sym.st_info) == stb_weak) {
                        out = 0;
                        return true;
                    }
                    return false;
                }
                out = resolved;
                return true;
            }
            if (sym.st_shndx == shn_abs) {
                out = sym.st_value;
                return true;
            }
            if (sym.st_shndx == shn_common) {
                return false; // COMMON not supported in this first cut
            }
            if (sym.st_shndx >= v.shnum || sec_base[sym.st_shndx] == nullptr) {
                return false; // symbol lives in a non-allocated section
            }
            const elf64_shdr& section = v.sh[sym.st_shndx];
            if (sym.st_value >= section.sh_size ||
                sym.st_value > ~u64{0} - reinterpret_cast<u64>(sec_base[sym.st_shndx]))
                return false;
            out = reinterpret_cast<u64>(sec_base[sym.st_shndx]) + sym.st_value;
            return true;
        }

    } // namespace detail

    // ========================================================================
    //  Shared entry points
    // ========================================================================

    kind kind_of(span<const u8> bytes) {
        u16 type = 0;
        if (!ident_type(bytes, type)) {
            return kind::none;
        }
        switch (type) {
        case et_rel:
            return kind::relocatable;
        case et_exec:
            return kind::executable;
        case et_dyn:
            return kind::dyn;
        default:
            return kind::none;
        }
    }

    bool load(span<const u8> bytes, const env& e, image& out) {
        switch (kind_of(bytes)) {
        case kind::relocatable:
            return load_relocatable(bytes, e, out);
        case kind::executable:
        case kind::dyn:
            return load_executable(bytes, e, out);
        default:
            return false;
        }
    }

} // namespace astia::elf
