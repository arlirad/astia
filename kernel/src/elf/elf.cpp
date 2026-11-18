// x86-64 ELF format parsing and relocatable (ET_REL) loading.
//
// Structure layouts and constants below are taken verbatim from the System V
// ABI x86-64 supplement (mirrored by glibc's <elf.h>). They are redeclared
// locally so the kernel build stays freestanding and the host test does not
// depend on a system header.

#include "elf_internal.hpp"

namespace astia::elf {

    using namespace detail;

    namespace {

        bool str_eq(const char* a, const char* b) {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }

        // ====================================================================
        //  Relocatable (ET_REL) path
        // ====================================================================

        // An ELF image viewed as validated pointers into the caller's buffer.
        struct view {
            const u8*         base     = nullptr;
            usize             len      = 0;
            const elf64_ehdr* eh       = nullptr;
            const elf64_shdr* sh       = nullptr;
            u16               shnum    = 0;
            const elf64_sym*  sym      = nullptr;
            usize             symcount = 0;
            const char*       str      = nullptr; // symbol string table
            usize             strsize  = 0;
        };

        bool symbol_name(const view& v, const elf64_sym& sym, const char*& out) {
            if (sym.st_name >= v.strsize)
                return false;
            const char* name = v.str + sym.st_name;
            const usize max = v.strsize - sym.st_name;
            for (usize i = 0; i < max; ++i) {
                if (name[i] == '\0') {
                    out = name;
                    return true;
                }
            }
            return false;
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

            // Locate SHT_SYMTAB and follow sh_link to its SHT_STRTAB.
            for (u16 i = 0; i < v.shnum; ++i) {
                const elf64_shdr& sh = v.sh[i];
                if (sh.sh_type != sht_symtab) {
                    continue;
                }
                if (sh.sh_entsize != sizeof(elf64_sym)) {
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
                v.sym      = reinterpret_cast<const elf64_sym*>(v.base + sh.sh_offset);
                v.symcount = sh.sh_size / sizeof(elf64_sym);
                v.str      = reinterpret_cast<const char*>(v.base + str.sh_offset);
                v.strsize  = static_cast<usize>(str.sh_size);
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
                u64         resolved = e.resolve != nullptr ? e.resolve(e.ctx, name) : 0;
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
            out = reinterpret_cast<u64>(sec_base[sym.st_shndx]) + sym.st_value;
            return true;
        }

    } // namespace

    // ========================================================================
    //  Relocatable (ET_REL) loader
    // ========================================================================

    bool load_relocatable(span<const u8> bytes, const env& e, image& out) {
        const usize len = bytes.size();
        if (e.map == nullptr || e.unmap == nullptr) {
            return false;
        }

        view v;
        if (!parse_relocatable(bytes, v)) {
            return false;
        }

        // --- Pass 1: compute a contiguous layout for the SHF_ALLOC sections.
        // Keeping every section in one block guarantees internal PC32
        // relocations stay within 32-bit range, and gives us a single
        // allocation to hand back for later unload.
        void* sec_base_storage[max_sections] = {};
        u64   offset                         = 0;
        u64   max_align                      = 1;
        bool  any_alloc                      = false;

        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& sh = v.sh[i];
            if ((sh.sh_flags & shf_alloc) == 0) {
                continue;
            }
            u64 align = sh.sh_addralign != 0 ? sh.sh_addralign : 1;
            if (align > max_align) {
                max_align = align;
            }
            // Round offset up to the section's alignment.
            offset = (offset + align - 1) & ~(align - 1);
            offset += sh.sh_size;
            any_alloc = true;
        }
        if (!any_alloc || offset == 0) {
            return false;
        }

        void* block = e.map(e.ctx, 0, static_cast<usize>(offset),
                            static_cast<usize>(max_align), false);
        if (block == nullptr) {
            return false;
        }

        struct mapped_block {
            const env& environment;
            void*      address;
            usize      size;

            ~mapped_block() {
                if (address != nullptr)
                    environment.unmap(environment.ctx, address, size);
            }

            void keep() { address = nullptr; }
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
            u64 align           = sh.sh_addralign != 0 ? sh.sh_addralign : 1;
            offset              = (offset + align - 1) & ~(align - 1);
            u8* dst             = static_cast<u8*>(block) + offset;
            sec_base_storage[i] = dst;

            // SHT_NOBITS (.bss) occupies memory but has no file bytes; the
            // block was zeroed above, so nothing to copy.
            if (sh.sh_type != sht_nobits) {
                if (!in_bounds(sh.sh_offset, sh.sh_size, len)) {
                    return false;
                }
                const u8* src = v.base + sh.sh_offset;
                for (u64 k = 0; k < sh.sh_size; ++k) {
                    dst[k] = src[k];
                }
            }
            offset += sh.sh_size;
        }

        const void* const* sec_base = sec_base_storage;

        // --- Apply relocations. Each SHT_RELA section names the section it
        // patches (sh_info) and the symbol table it uses (sh_link).
        for (u16 i = 0; i < v.shnum; ++i) {
            const elf64_shdr& sh = v.sh[i];
            if (sh.sh_type != sht_rela) {
                continue;
            }
            if (sh.sh_info >= v.shnum) {
                return false;
            }
            // A relocation against a section we did not load (.comment,
            // .debug_*, ...) patches data that is not in memory; skip it.
            if (sec_base[sh.sh_info] == nullptr) {
                continue;
            }
            if (sh.sh_entsize != sizeof(elf64_rela) ||
                !in_bounds(sh.sh_offset, sh.sh_size, len)) {
                return false;
            }
            u8*               target = static_cast<u8*>(sec_base_storage[sh.sh_info]);
            u64               target_size = v.sh[sh.sh_info].sh_size;
            usize             count       = sh.sh_size / sizeof(elf64_rela);
            const elf64_rela* rela =
                reinterpret_cast<const elf64_rela*>(v.base + sh.sh_offset);

            for (usize k = 0; k < count; ++k) {
                const elf64_rela& r    = rela[k];
                u32               type = r_type(r.r_info);
                if (type == r_x86_64_none) {
                    continue;
                }

                // The widest store below is 8 bytes; make sure the slot lies
                // inside the section it patches.
                u64 wsize = (type == r_x86_64_64) ? 8 : 4;
                if (r.r_offset > target_size || wsize > target_size - r.r_offset) {
                    return false;
                }

                u64 s = 0;
                if (!sym_addr(v, e, sec_base, r_sym(r.r_info), s)) {
                    return false;
                }
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
                    if (val > 0xffffffffull) {
                        return false;
                    }
                    write_u32(reinterpret_cast<u8*>(p), static_cast<u32>(val));
                    break;
                }
                case r_x86_64_32s: {
                    i64 val = static_cast<i64>(s) + a;
                    if (val < -2147483648LL || val > 2147483647LL) {
                        return false;
                    }
                    write_u32(reinterpret_cast<u8*>(p), static_cast<u32>(val));
                    break;
                }
                default:
                    return false; // unsupported relocation type
                }
            }
        }

        // --- Find the entry point: the defined symbol named "module_main".
        for (usize i = 0; i < v.symcount; ++i) {
            const elf64_sym& sym = v.sym[i];
            if (sym.st_shndx == shn_undef || sym.st_shndx >= v.shnum) {
                continue;
            }
            const char* name = nullptr;
            if (!symbol_name(v, sym, name)) {
                return false;
            }
            if (!str_eq(name, "module_main")) {
                continue;
            }
            u64 addr = 0;
            if (!sym_addr(v, e, sec_base, static_cast<u32>(i), addr)) {
                return false;
            }
            out.entry     = reinterpret_cast<void*>(addr);
            out.base      = block;
            out.size      = static_cast<usize>(offset);
            out.loaded    = kind::relocatable;
            out.load_bias = 0;
            out.nsegs     = 0;
            mapped.keep();
            return true;
        }

        // No module_main symbol.
        return false;
    }

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
