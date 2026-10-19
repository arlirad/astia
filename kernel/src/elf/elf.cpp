// x86-64 ELF loader: relocatable (ET_REL) and linked executable (ET_EXEC/ET_DYN).
//
// Structure layouts and constants below are taken verbatim from the System V
// ABI x86-64 supplement (mirrored by glibc's <elf.h>). They are redeclared
// locally so the kernel build stays freestanding and the host test does not
// depend on a system header.

#include <astia/elf.hpp>

namespace astia::elf {

    namespace {

        using u8  = astia::u8;
        using u16 = astia::u16;
        using u32 = astia::u32;
        using u64 = astia::u64;
        using i64 = astia::i64;

        // --- ELF constants (System V ABI, x86-64 supplement) ---

        constexpr usize ei_nident    = 16;
        constexpr u32   ei_class     = 4;
        constexpr u32   ei_data      = 5;
        constexpr u8    elfclass64   = 2;
        constexpr u8    elfdatal2lsb = 1;

        constexpr u16 et_rel    = 1;
        constexpr u16 et_exec   = 2;
        constexpr u16 et_dyn    = 3;
        constexpr u16 em_x86_64 = 62;

        constexpr u32 sht_symtab = 2;
        constexpr u32 sht_strtab = 3;
        constexpr u32 sht_rela   = 4;
        constexpr u32 sht_nobits = 8;

        constexpr u64 shf_alloc = 1ull << 1;

        constexpr u16 shn_undef  = 0;
        constexpr u16 shn_abs    = 0xfff1;
        constexpr u16 shn_common = 0xfff2;

        // Program header types (only the two a segment loader cares about).
        constexpr u32 pt_load    = 1;
        constexpr u32 pt_dynamic = 2;
        constexpr u32 pt_tls     = 7;

        // Segment permission flags (p_flags / segment::flags).
        constexpr u32 pf_x = 1;
        constexpr u32 pf_w = 2;
        constexpr u32 pf_r = 4;

        // .dynamic tags.
        constexpr i64 dt_null    = 0;
        constexpr i64 dt_rela    = 7;
        constexpr i64 dt_relasz  = 8;
        constexpr i64 dt_relaent = 9;

        // Relocation types this loader understands (x86-64).
        constexpr u32 r_x86_64_none     = 0;
        constexpr u32 r_x86_64_64       = 1;
        constexpr u32 r_x86_64_pc32     = 2;
        constexpr u32 r_x86_64_plt32    = 4;
        constexpr u32 r_x86_64_relative = 8;
        constexpr u32 r_x86_64_32       = 10;
        constexpr u32 r_x86_64_32s      = 11;

        // --- On-disk structures ---

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

        // --- Field accessors (System V ABI accessor macros) ---

        constexpr u8 st_bind(u8 info) {
            return info >> 4;
        }

        constexpr u32 r_sym(u64 info) {
            return static_cast<u32>(info >> 32);
        }
        constexpr u32 r_type(u64 info) {
            return static_cast<u32>(info & 0xffffffffu);
        }

        constexpr u8 stb_weak = 2;

        // Upper bound on sections we will process. A typical module .o has
        // ~15-20; the fixed cap keeps the per-section base table off the heap
        // (this loader must work before/independent of any dynamic allocation,
        // and mirrors memory_map / init's fixed-capacity style).
        constexpr usize max_sections = 64;

        bool str_eq(const char* a, const char* b) {
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }

        // True if [off, off+size) lies inside a len-byte buffer.
        bool in_bounds(u64 off, u64 size, usize len) {
            return off <= len && size <= len - off;
        }

        constexpr u64 align_down(u64 v) {
            return v & ~(page_size - 1);
        }
        constexpr u64 align_up(u64 v) {
            return (v + page_size - 1) & ~(page_size - 1);
        }

        // Validate the ELF64/x86-64 identity and return e_type, or 0 on any
        // mismatch. Shared by kind_of() and both loaders.
        bool ident_type(const void* bytes, usize len, u16& type_out) {
            if (bytes == nullptr || len < sizeof(elf64_ehdr)) {
                return false;
            }
            const auto* eh = static_cast<const elf64_ehdr*>(bytes);
            if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' ||
                eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
                return false;
            }
            if (eh->e_ident[ei_class] != elfclass64 ||
                eh->e_ident[ei_data] != elfdatal2lsb) {
                return false;
            }
            if (eh->e_machine != em_x86_64) {
                return false;
            }
            type_out = eh->e_type;
            return true;
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

        // Validate the section table, then locate the symbol table and its
        // linked string table. Returns false on anything malformed or
        // truncated.
        bool parse_relocatable(const void* bytes, usize len, view& v) {
            u16 type = 0;
            if (!ident_type(bytes, len, type) || type != et_rel) {
                return false;
            }
            v.base = static_cast<const u8*>(bytes);
            v.len  = len;
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
                if (sym.st_name >= v.strsize) {
                    return false; // name offset runs past the string table
                }
                const char* name     = v.str + sym.st_name;
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

    bool load_relocatable(const void* bytes, usize len, const env& e, image& out) {
        if (e.map == nullptr) {
            return false;
        }

        view v;
        if (!parse_relocatable(bytes, len, v)) {
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
                    *reinterpret_cast<u64*>(p) = s + static_cast<u64>(a);
                    break;
                }
                case r_x86_64_pc32:
                case r_x86_64_plt32: {
                    i64 rel = static_cast<i64>(s) + a - static_cast<i64>(p);
                    if (rel < -2147483648LL || rel > 2147483647LL) {
                        return false; // does not fit signed 32-bit
                    }
                    *reinterpret_cast<u32*>(p) = static_cast<u32>(rel);
                    break;
                }
                case r_x86_64_32: {
                    u64 val = s + static_cast<u64>(a);
                    if (val > 0xffffffffull) {
                        return false;
                    }
                    *reinterpret_cast<u32*>(p) = static_cast<u32>(val);
                    break;
                }
                case r_x86_64_32s: {
                    i64 val = static_cast<i64>(s) + a;
                    if (val < -2147483648LL || val > 2147483647LL) {
                        return false;
                    }
                    *reinterpret_cast<u32*>(p) = static_cast<u32>(val);
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
            if (sym.st_name >= v.strsize) {
                continue;
            }
            if (!str_eq(v.str + sym.st_name, "module_main")) {
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
            return true;
        }

        // No module_main symbol.
        return false;
    }

    // ========================================================================
    //  Linked executable (ET_EXEC / ET_DYN) loader
    // ========================================================================

    namespace {

        // Translate a virtual address to a file offset using the PT_LOAD
        // segments (the only segments whose bytes live in the file).
        bool vaddr_to_off(const elf64_phdr* ph, u16 phnum, u64 vaddr, usize len,
                          u64& off) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load) {
                    continue;
                }
                if (vaddr >= ph[i].p_vaddr && vaddr < ph[i].p_vaddr + ph[i].p_filesz) {
                    u64 o = ph[i].p_offset + (vaddr - ph[i].p_vaddr);
                    if (o > len) {
                        return false;
                    }
                    off = o;
                    return true;
                }
            }
            return false;
        }

        // Apply the R_X86_64_RELATIVE dynamic relocations named by .dynamic.
        // A PIE's link-time absolute pointers are stored link-time-relative;
        // each needs `*loc = bias + addend` to be usable at its run address.
        // Any other dynamic relocation type means real dynamic linking, which
        // this loader deliberately does not do, so we reject it.
        bool apply_relative_relocs(const u8* bytes, usize len, const elf64_phdr* ph,
                                   u16 phnum, u64 load_bias, u64 span_start,
                                   u64 span_end) {
            // Find PT_DYNAMIC; a fully static ET_EXEC has none, which is fine.
            const elf64_dyn* dyn       = nullptr;
            u64              dyn_count = 0;
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type == pt_dynamic) {
                    if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                        return false;
                    }
                    dyn = reinterpret_cast<const elf64_dyn*>(bytes + ph[i].p_offset);
                    dyn_count = ph[i].p_filesz / sizeof(elf64_dyn);
                    break;
                }
            }
            if (dyn == nullptr) {
                return true; // nothing to relocate
            }

            // Locate DT_RELA (a virtual address) and its size.
            u64 rela_vaddr = 0;
            u64 rela_size  = 0;
            for (u64 i = 0; i < dyn_count; ++i) {
                if (dyn[i].d_tag == dt_null) {
                    break;
                }
                if (dyn[i].d_tag == dt_rela) {
                    rela_vaddr = dyn[i].d_val;
                }
                else if (dyn[i].d_tag == dt_relasz) {
                    rela_size = dyn[i].d_val;
                }
                else if (dyn[i].d_tag == dt_relaent) {
                    if (dyn[i].d_val != sizeof(elf64_rela)) {
                        return false; // unexpected RELA entry size
                    }
                }
            }
            if (rela_vaddr == 0 || rela_size == 0) {
                return true; // no dynamic relocations
            }

            u64 rela_off = 0;
            if (!vaddr_to_off(ph, phnum, rela_vaddr, len, rela_off)) {
                return false;
            }
            if (!in_bounds(rela_off, rela_size, len)) {
                return false;
            }
            u64         count = rela_size / sizeof(elf64_rela);
            const auto* rela  = reinterpret_cast<const elf64_rela*>(bytes + rela_off);

            for (u64 k = 0; k < count; ++k) {
                const elf64_rela& r    = rela[k];
                u32               type = r_type(r.r_info);
                if (type == r_x86_64_none) {
                    continue;
                }
                if (type != r_x86_64_relative) {
                    return false; // needs full dynamic linking, which we do not do
                }
                // The patched slot lives in the mapped image at vaddr + bias.
                u64 loc_vaddr = r.r_offset;
                if (loc_vaddr < span_start || loc_vaddr + sizeof(u64) > span_end) {
                    return false;
                }
                u64* loc = reinterpret_cast<u64*>(load_bias + loc_vaddr);
                *loc     = load_bias + static_cast<u64>(r.r_addend);
            }
            return true;
        }

    } // namespace

    bool load_executable(const void* bytes, usize len, const env& e, image& out) {
        if (e.map == nullptr) {
            return false;
        }
        u16 type = 0;
        if (!ident_type(bytes, len, type) || (type != et_exec && type != et_dyn)) {
            return false;
        }
        const auto* eh         = static_cast<const elf64_ehdr*>(bytes);
        const u8*   base_bytes = static_cast<const u8*>(bytes);

        if (eh->e_phnum == 0 || eh->e_phentsize != sizeof(elf64_phdr)) {
            return false;
        }
        if (!in_bounds(eh->e_phoff, static_cast<u64>(eh->e_phnum) * sizeof(elf64_phdr),
                       len)) {
            return false;
        }
        const auto* ph    = reinterpret_cast<const elf64_phdr*>(base_bytes + eh->e_phoff);
        u16         phnum = eh->e_phnum;

        // --- Pass 1: collect PT_LOAD segments and the total span they cover.
        // Mapped as one contiguous reservation so relative vaddr layout is
        // preserved; the load bias shifts the whole set at once.
        u64   min_vaddr = ~u64{0};
        u64   max_end   = 0;
        usize nsegs     = 0;
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_load) {
                continue;
            }
            if (nsegs >= max_segments) {
                return false; // too many segments for our fixed cap
            }
            if (ph[i].p_memsz < ph[i].p_filesz) {
                return false;
            }
            if (ph[i].p_vaddr < min_vaddr) {
                min_vaddr = ph[i].p_vaddr;
            }
            if (ph[i].p_vaddr + ph[i].p_memsz > max_end) {
                max_end = ph[i].p_vaddr + ph[i].p_memsz;
            }
            ++nsegs;
        }
        if (nsegs == 0 || min_vaddr == ~u64{0} || max_end <= min_vaddr) {
            return false;
        }

        u64 span_start = align_down(min_vaddr);
        u64 span_end   = align_up(max_end);
        u64 span       = span_end - span_start;

        // --- Reserve the span. ET_EXEC is linked at fixed addresses and must
        // land exactly there; ET_DYN is position-independent, so the allocator
        // picks the base and we derive the bias from it.
        bool  fixed  = (type == et_exec);
        void* mapped = e.map(e.ctx, span_start, static_cast<usize>(span),
                             static_cast<usize>(page_size), fixed);
        if (mapped == nullptr) {
            return false;
        }
        u64 mapped_addr = reinterpret_cast<u64>(mapped);
        if (fixed && mapped_addr != span_start) {
            return false; // fixed-address map ignored the requested address
        }
        u64 load_bias = mapped_addr - span_start;

        // map_fn allocates zeroed frames, so the whole span (including BSS
        // and inter-segment gaps) is already clear.  Do not dereference the
        // freshly mapped user virtual range from this kernel context.

        // --- Pass 2: copy each segment's file bytes into place.
        usize seg_i = 0;
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_load) {
                continue;
            }
            if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                return false;
            }
            u64       dest = ph[i].p_vaddr + load_bias;
            const u8* src  = base_bytes + ph[i].p_offset;
            u8*       d    = nullptr;
            for (usize mapped_i = 0; mapped_i < seg_i; ++mapped_i) {
                const segment& mapped_seg = out.segs[mapped_i];
                if (dest >= reinterpret_cast<u64>(mapped_seg.loc) &&
                    dest < reinterpret_cast<u64>(mapped_seg.loc) + mapped_seg.memsz) {
                    d = static_cast<u8*>(mapped_seg.loc) + (dest -
                        reinterpret_cast<u64>(mapped_seg.loc));
                    break;
                }
            }
            if (e.copy != nullptr) {
                if (!e.copy(e.ctx, dest, src, static_cast<usize>(ph[i].p_filesz)))
                    return false;
            }
            else if (d == nullptr) {
                d = reinterpret_cast<u8*>(dest);
            }
            if (e.copy == nullptr)
                for (u64 k = 0; k < ph[i].p_filesz; ++k) d[k] = src[k];
            segment& seg = out.segs[seg_i++];
            seg.vaddr    = ph[i].p_vaddr;
            seg.filesz   = ph[i].p_filesz;
            seg.memsz    = ph[i].p_memsz;
            seg.flags    = ph[i].p_flags;
            seg.loc      = reinterpret_cast<void*>(dest);
        }

        // --- Apply R_X86_64_RELATIVE dynamic relocations (a PIE needs them to
        // make its internal pointers valid at the run address).
        if (!apply_relative_relocs(base_bytes, len, ph, phnum, load_bias, span_start,
                                   span_end)) {
            return false;
        }

        // --- Static TLS: record the PT_TLS template for whoever
        // materializes it per thread (the userspace loader).
        for (u16 i = 0; i < phnum; ++i) {
            if (ph[i].p_type != pt_tls) {
                continue;
            }
            if (ph[i].p_memsz != 0) {
                if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                    return false;
                }
                out.tls_offset = ph[i].p_offset;
                out.tls_filesz = ph[i].p_filesz;
                out.tls_memsz  = ph[i].p_memsz;
                out.tls_align  = ph[i].p_align;
            }
            break;
        }

        out.entry     = reinterpret_cast<void*>(eh->e_entry + load_bias);
        out.base      = mapped;
        out.size      = static_cast<usize>(span);
        out.loaded    = (type == et_dyn) ? kind::dyn : kind::executable;
        out.load_bias = load_bias;
        out.nsegs     = seg_i;
        return true;
    }

    bool load_executable_stream(const env& e, usize file_size, image& out) {
        if (e.map == nullptr || e.read_batch == nullptr || file_size < sizeof(elf64_ehdr))
            return false;
        elf64_ehdr header{};
        read_request head_request{0, &header, sizeof header, -1};
        if (!e.read_batch(e.ctx, &head_request, 1) ||
            head_request.result != static_cast<long>(sizeof header))
            return false;

        u16 type = 0;
        if (!ident_type(&header, sizeof header, type) ||
            (type != et_exec && type != et_dyn) || header.e_phnum == 0 ||
            header.e_phentsize != sizeof(elf64_phdr))
            return false;
        if (header.e_phnum > 64 ||
            !in_bounds(header.e_phoff,
                       static_cast<u64>(header.e_phnum) * sizeof(elf64_phdr), file_size))
            return false;

        elf64_phdr ph[64]{};
        read_request ph_request{
            header.e_phoff, ph,
            static_cast<usize>(header.e_phnum) * sizeof(elf64_phdr), -1};
        if (!e.read_batch(e.ctx, &ph_request, 1) ||
            ph_request.result != static_cast<long>(ph_request.length))
            return false;

        u64 min_vaddr = ~u64{0}, max_end = 0;
        usize nsegs = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type == pt_dynamic)
                return false; // relocations need a second metadata read path
            if (ph[i].p_type != pt_load) continue;
            if (nsegs >= max_segments || ph[i].p_memsz < ph[i].p_filesz ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size))
                return false;
            min_vaddr = ph[i].p_vaddr < min_vaddr ? ph[i].p_vaddr : min_vaddr;
            const u64 end = ph[i].p_vaddr + ph[i].p_memsz;
            if (end < ph[i].p_vaddr) return false;
            max_end = end > max_end ? end : max_end;
            ++nsegs;
        }
        if (nsegs == 0 || min_vaddr == ~u64{0} || max_end <= min_vaddr)
            return false;

        const u64 span_start = align_down(min_vaddr);
        const u64 span_end = align_up(max_end);
        const u64 span = span_end - span_start;
        const bool fixed = type == et_exec;
        void* mapped = e.map(e.ctx, span_start, static_cast<usize>(span), page_size, fixed);
        if (mapped == nullptr || (fixed && reinterpret_cast<u64>(mapped) != span_start))
            return false;
        const u64 bias = reinterpret_cast<u64>(mapped) - span_start;
        for (u64 i = 0; i < span; ++i)
            static_cast<u8*>(mapped)[i] = 0;

        read_request requests[max_segments]{};
        usize request_count = 0;
        usize segment_count = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_load) continue;
            if (ph[i].p_filesz != 0) {
                requests[request_count++] = {
                    ph[i].p_offset,
                    reinterpret_cast<void*>(ph[i].p_vaddr + bias),
                    static_cast<usize>(ph[i].p_filesz), -1};
            }
            out.segs[segment_count++] = {
                ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_memsz, ph[i].p_flags,
                reinterpret_cast<void*>(ph[i].p_vaddr + bias)};
        }
        if (request_count != 0 &&
            (!e.read_batch(e.ctx, requests, request_count)))
            return false;
        for (usize i = 0; i < request_count; ++i)
            if (requests[i].result != static_cast<long>(requests[i].length)) return false;

        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type == pt_tls && ph[i].p_memsz != 0) {
                if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size)) return false;
                out.tls_offset = ph[i].p_offset;
                out.tls_filesz = ph[i].p_filesz;
                out.tls_memsz = ph[i].p_memsz;
                out.tls_align = ph[i].p_align;
            }
        }
        out.entry = reinterpret_cast<void*>(header.e_entry + bias);
        out.base = mapped;
        out.size = static_cast<usize>(span);
        out.loaded = type == et_dyn ? kind::dyn : kind::executable;
        out.load_bias = bias;
        out.nsegs = segment_count;
        return true;
    }

    // ========================================================================
    //  Shared entry points
    // ========================================================================

    kind kind_of(const void* bytes, usize len) {
        u16 type = 0;
        if (!ident_type(bytes, len, type)) {
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

    bool load(const void* bytes, usize len, const env& e, image& out) {
        switch (kind_of(bytes, len)) {
        case kind::relocatable:
            return load_relocatable(bytes, len, e, out);
        case kind::executable:
        case kind::dyn:
            return load_executable(bytes, len, e, out);
        default:
            return false;
        }
    }

} // namespace astia::elf
