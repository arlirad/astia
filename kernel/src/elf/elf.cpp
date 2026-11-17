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

        constexpr usize ei_nident    = elf_ident_size;
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
        bool ident_type(span<const u8> bytes, u16& type_out) {
            if (bytes.data() == nullptr || bytes.size() < sizeof(elf64_ehdr)) {
                return false;
            }
            const auto* eh = reinterpret_cast<const elf64_ehdr*>(bytes.data());
            if (eh->e_ident[0] != elf_magic[0] || eh->e_ident[1] != elf_magic[1] ||
                eh->e_ident[2] != elf_magic[2] || eh->e_ident[3] != elf_magic[3]) {
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
                const usize name_max = v.strsize - sym.st_name;
                usize       name_len = 0;
                while (name_len < name_max && name[name_len] != '\0')
                    ++name_len;
                if (name_len == name_max)
                    return false; // symbol name is not terminated in .strtab
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
        if (e.map == nullptr) {
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

        struct async_mapping {
            const async_env& env;
            void*             address;
            usize             size;

            void reset() {
                if (address != nullptr) {
                    env.unmap(env.ctx, address, size);
                    address = nullptr;
                }
            }

            void dismiss() { address = nullptr; }

            ~async_mapping() { reset(); }
        };

        bool vaddr_range_to_off(const elf64_phdr* ph, u16 phnum, u64 vaddr, u64 size,
                                usize len, u64& off) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load ||
                    !in_bounds(ph[i].p_offset, ph[i].p_filesz, len) ||
                    vaddr < ph[i].p_vaddr ||
                    vaddr - ph[i].p_vaddr > ph[i].p_filesz ||
                    size > ph[i].p_filesz - (vaddr - ph[i].p_vaddr))
                    continue;
                off = ph[i].p_offset + (vaddr - ph[i].p_vaddr);
                return true;
            }
            return false;
        }

        bool vaddr_range_in_load(const elf64_phdr* ph, u16 phnum, u64 vaddr,
                                 u64 size) {
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type != pt_load || vaddr < ph[i].p_vaddr ||
                    vaddr - ph[i].p_vaddr > ph[i].p_memsz ||
                    size > ph[i].p_memsz - (vaddr - ph[i].p_vaddr))
                    continue;
                return true;
            }
            return false;
        }

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
        bool apply_relative_relocs(span<const u8> bytes, const elf64_phdr* ph, u16 phnum,
                                   u64 load_bias, u64 span_start, u64 span_end) {
            const usize len = bytes.size();
            // Find PT_DYNAMIC; a fully static ET_EXEC has none, which is fine.
            const elf64_dyn* dyn       = nullptr;
            u64              dyn_count = 0;
            for (u16 i = 0; i < phnum; ++i) {
                if (ph[i].p_type == pt_dynamic) {
                    if (!in_bounds(ph[i].p_offset, ph[i].p_filesz, len)) {
                        return false;
                    }
                    dyn =
                        reinterpret_cast<const elf64_dyn*>(bytes.data() + ph[i].p_offset);
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
            const auto* rela =
                reinterpret_cast<const elf64_rela*>(bytes.data() + rela_off);

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

    bool load_executable(span<const u8> bytes, const env& e, image& out) {
        const usize len = bytes.size();
        if (e.map == nullptr) {
            return false;
        }
        u16 type = 0;
        if (!ident_type(bytes, type) || (type != et_exec && type != et_dyn)) {
            return false;
        }
        const auto* eh         = reinterpret_cast<const elf64_ehdr*>(bytes.data());
        const u8*   base_bytes = bytes.data();

        if (eh->e_phnum == 0 || eh->e_phentsize != sizeof(elf64_phdr)) {
            return false;
        }
        if (!in_bounds(eh->e_phoff, static_cast<u64>(eh->e_phnum) * sizeof(elf64_phdr),
                       bytes.size())) {
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
                    d = static_cast<u8*>(mapped_seg.loc) +
                        (dest - reinterpret_cast<u64>(mapped_seg.loc));
                    break;
                }
            }
            if (e.copy != nullptr) {
                if (!e.copy(
                        e.ctx, dest,
                        astia::span<const u8>(src, static_cast<usize>(ph[i].p_filesz))))
                    return false;
            }
            else if (d == nullptr) {
                d = reinterpret_cast<u8*>(dest);
            }
            if (e.copy == nullptr)
                for (u64 k = 0; k < ph[i].p_filesz; ++k)
                    d[k] = src[k];
            segment& seg = out.segs[seg_i++];
            seg.vaddr    = ph[i].p_vaddr;
            seg.filesz   = ph[i].p_filesz;
            seg.memsz    = ph[i].p_memsz;
            seg.flags    = ph[i].p_flags;
            seg.loc      = reinterpret_cast<void*>(dest);
        }

        // --- Apply R_X86_64_RELATIVE dynamic relocations (a PIE needs them to
        // make its internal pointers valid at the run address).
        if (!apply_relative_relocs(bytes, ph, phnum, load_bias, span_start, span_end)) {
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

    core::task<image> load_executable_stream(const async_env& e, usize file_size) {
        image result{};
        constexpr usize read_chunk_size = 4096;
        constexpr usize rela_chunk_size = sizeof(elf64_rela) * 170;

        if (e.map == nullptr || e.unmap == nullptr || e.read == nullptr ||
            file_size < sizeof(elf64_ehdr))
            co_return error_t::einval;

        elf64_ehdr header{};
        auto header_read = co_await e.read(
            e.ctx, span<u8>(reinterpret_cast<u8*>(&header), sizeof header), 0);
        if (!header_read)
            co_return header_read.error == error_t::enone ? error_t::eio : header_read.error;
        if (header_read.value != static_cast<i64>(sizeof header))
            co_return error_t::eio;

        u16 type = 0;
        if (!ident_type(
                span<const u8>(reinterpret_cast<const u8*>(&header), sizeof header), type) ||
            (type != et_exec && type != et_dyn) || header.e_phnum == 0 ||
            header.e_phentsize != sizeof(elf64_phdr) ||
            header.e_phnum > max_program_headers ||
            !in_bounds(header.e_phoff,
                       static_cast<u64>(header.e_phnum) * sizeof(elf64_phdr), file_size))
            co_return error_t::einval;

        elf64_phdr ph[max_program_headers]{};
        const usize ph_bytes = static_cast<usize>(header.e_phnum) * sizeof(elf64_phdr);
        auto ph_read = co_await e.read(
            e.ctx, span<u8>(reinterpret_cast<u8*>(ph), ph_bytes), header.e_phoff);
        if (!ph_read)
            co_return ph_read.error == error_t::enone ? error_t::eio : ph_read.error;
        if (ph_read.value != static_cast<i64>(ph_bytes))
            co_return error_t::eio;

        u64   min_vaddr = ~u64{0};
        u64   max_end   = 0;
        usize nsegs     = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_load)
                continue;
            if (nsegs >= max_segments || ph[i].p_memsz < ph[i].p_filesz ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size) ||
                ph[i].p_vaddr > ~u64{0} - ph[i].p_memsz)
                co_return error_t::einval;
            min_vaddr = ph[i].p_vaddr < min_vaddr ? ph[i].p_vaddr : min_vaddr;
            const u64 end = ph[i].p_vaddr + ph[i].p_memsz;
            max_end       = end > max_end ? end : max_end;
            ++nsegs;
        }
        if (nsegs == 0 || min_vaddr == ~u64{0} || max_end <= min_vaddr ||
            max_end > ~u64{0} - (page_size - 1))
            co_return error_t::einval;

        const u64 span_start = align_down(min_vaddr);
        const u64 span_end   = align_up(max_end);
        if (span_end <= span_start)
            co_return error_t::einval;
        const u64 image_span = span_end - span_start;
        if (image_span > static_cast<u64>(~usize{0}))
            co_return error_t::einval;

        u64 rela_vaddr = 0;
        u64 rela_size  = 0;
        bool rela_tag   = false;
        bool relasz_tag = false;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_dynamic)
                continue;
            if (ph[i].p_filesz == 0 || ph[i].p_filesz % sizeof(elf64_dyn) != 0 ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size))
                co_return error_t::einval;

            alignas(elf64_dyn) u8 buffer[read_chunk_size];
            u64 offset = 0;
            bool terminated = false;
            while (offset < ph[i].p_filesz) {
                const u64 remaining = ph[i].p_filesz - offset;
                const usize length = remaining < read_chunk_size
                                         ? static_cast<usize>(remaining)
                                         : read_chunk_size;
                auto read = co_await e.read(
                    e.ctx, span<u8>(buffer, length), ph[i].p_offset + offset);
                if (!read)
                    co_return read.error == error_t::enone ? error_t::eio : read.error;
                if (read.value != static_cast<i64>(length))
                    co_return error_t::eio;
                const usize count = length / sizeof(elf64_dyn);
                const auto* entries = reinterpret_cast<const elf64_dyn*>(buffer);
                for (usize k = 0; k < count; ++k) {
                    if (entries[k].d_tag == dt_null) {
                        terminated = true;
                        break;
                    }
                    if (entries[k].d_tag == dt_rela) {
                        rela_vaddr = entries[k].d_val;
                        rela_tag = true;
                    }
                    else if (entries[k].d_tag == dt_relasz) {
                        rela_size = entries[k].d_val;
                        relasz_tag = true;
                    }
                    else if (entries[k].d_tag == dt_relaent) {
                        if (entries[k].d_val != sizeof(elf64_rela))
                            co_return error_t::einval;
                    }
                }
                offset += length;
                if (terminated)
                    break;
            }
            if (!terminated)
                co_return error_t::einval;
            break;
        }

        if (rela_tag != relasz_tag || rela_size % sizeof(elf64_rela) != 0 ||
            (rela_size != 0 && rela_vaddr == 0))
            co_return error_t::einval;

        u64 rela_offset = 0;
        if (rela_vaddr != 0 && rela_size != 0) {
            if (!vaddr_range_to_off(ph, header.e_phnum, rela_vaddr, rela_size, file_size,
                                    rela_offset) ||
                !in_bounds(rela_offset, rela_size, file_size))
                co_return error_t::einval;
        }

        const bool fixed = type == et_exec;
        void* mapped = e.map(e.ctx, span_start, static_cast<usize>(image_span),
                             static_cast<usize>(page_size), fixed);
        if (mapped == nullptr)
            co_return error_t::enomem;
        async_mapping mapping{e, mapped, static_cast<usize>(image_span)};
        auto fail = [&mapping](error_t error) {
            mapping.reset();
            return error;
        };
        const u64 mapped_addr = reinterpret_cast<u64>(mapped);
        if ((fixed && mapped_addr != span_start) || image_span > ~u64{0} - mapped_addr)
            co_return fail(error_t::einval);
        const u64 bias = mapped_addr - span_start;

        if (e.copy == nullptr) {
            auto* zero = static_cast<u8*>(mapped);
            for (u64 i = 0; i < image_span; ++i)
                zero[i] = 0;
        }
        else {
            u8 zero[read_chunk_size] = {};
            u64 cleared = 0;
            while (cleared < image_span) {
                const u64 remaining = image_span - cleared;
                const usize length = remaining < read_chunk_size
                                         ? static_cast<usize>(remaining)
                                         : read_chunk_size;
                if (!e.copy(e.ctx, mapped_addr + cleared, span<const u8>(zero, length)))
                    co_return fail(error_t::eio);
                cleared += length;
            }
        }

        u8 segment_buffer[read_chunk_size];
        usize segment_count = 0;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_load)
                continue;
            const u64 destination = mapped_addr + (ph[i].p_vaddr - span_start);
            u64       copied      = 0;
            while (copied < ph[i].p_filesz) {
                const u64 remaining = ph[i].p_filesz - copied;
                const usize length = remaining < read_chunk_size
                                         ? static_cast<usize>(remaining)
                                         : read_chunk_size;
                u8* destination_buffer = e.copy != nullptr
                                             ? segment_buffer
                                             : reinterpret_cast<u8*>(destination + copied);
                auto read = co_await e.read(
                    e.ctx, span<u8>(destination_buffer, length), ph[i].p_offset + copied);
                if (!read)
                    co_return fail(read.error == error_t::enone ? error_t::eio : read.error);
                if (read.value != static_cast<i64>(length))
                    co_return fail(error_t::eio);
                if (e.copy != nullptr &&
                    !e.copy(e.ctx, destination + copied,
                            span<const u8>(segment_buffer, length)))
                    co_return fail(error_t::eio);
                copied += length;
            }
            result.segs[segment_count++] = {
                ph[i].p_vaddr, ph[i].p_filesz, ph[i].p_memsz, ph[i].p_flags,
                reinterpret_cast<void*>(destination)};
        }

        if (rela_vaddr != 0 && rela_size != 0) {
            alignas(elf64_rela) u8 rela_buffer[rela_chunk_size];
            u64 offset = 0;
            while (offset < rela_size) {
                const u64 remaining = rela_size - offset;
                const usize length = remaining < rela_chunk_size
                                         ? static_cast<usize>(remaining)
                                         : rela_chunk_size;
                auto read = co_await e.read(
                    e.ctx, span<u8>(rela_buffer, length), rela_offset + offset);
                if (!read)
                    co_return fail(read.error == error_t::enone ? error_t::eio : read.error);
                if (read.value != static_cast<i64>(length))
                    co_return fail(error_t::eio);
                const usize count = length / sizeof(elf64_rela);
                const auto* entries = reinterpret_cast<const elf64_rela*>(rela_buffer);
                for (usize k = 0; k < count; ++k) {
                    const elf64_rela& relocation = entries[k];
                    const u32 relocation_type = r_type(relocation.r_info);
                    if (relocation_type == r_x86_64_none)
                        continue;
                    if (relocation_type != r_x86_64_relative)
                        co_return fail(error_t::einval);
                    const u64 location_vaddr = relocation.r_offset;
                    if (location_vaddr < span_start || location_vaddr > span_end ||
                        sizeof(u64) > span_end - location_vaddr ||
                        !vaddr_range_in_load(ph, header.e_phnum, location_vaddr,
                                             sizeof(u64)))
                        co_return fail(error_t::einval);
                    const u64 location = mapped_addr + (location_vaddr - span_start);
                    const u64 value = bias + static_cast<u64>(relocation.r_addend);
                    if (e.copy != nullptr) {
                        u8 value_bytes[sizeof(value)];
                        for (usize byte = 0; byte < sizeof(value); ++byte)
                            value_bytes[byte] = static_cast<u8>(value >> (byte * 8));
                        if (!e.copy(e.ctx, location,
                                    span<const u8>(value_bytes, sizeof value_bytes)))
                            co_return fail(error_t::eio);
                    }
                    else {
                        *reinterpret_cast<u64*>(location) = value;
                    }
                }
                offset += length;
            }
        }

        bool tls_seen = false;
        for (u16 i = 0; i < header.e_phnum; ++i) {
            if (ph[i].p_type != pt_tls)
                continue;
            if (ph[i].p_filesz > ph[i].p_memsz ||
                !in_bounds(ph[i].p_offset, ph[i].p_filesz, file_size) ||
                (ph[i].p_align != 0 && (ph[i].p_align & (ph[i].p_align - 1)) != 0))
                co_return fail(error_t::einval);
            if (tls_seen)
                co_return fail(error_t::einval);
            tls_seen = true;
            if (ph[i].p_memsz == 0)
                continue;
            result.tls_offset = ph[i].p_offset;
            result.tls_filesz = ph[i].p_filesz;
            result.tls_memsz  = ph[i].p_memsz;
            result.tls_align  = ph[i].p_align;
        }

        if (header.e_entry < span_start || header.e_entry >= span_end ||
            !vaddr_range_in_load(ph, header.e_phnum, header.e_entry, 1))
            co_return fail(error_t::einval);
        result.entry = reinterpret_cast<void*>(mapped_addr + (header.e_entry - span_start));
        result.base      = mapped;
        result.size      = static_cast<usize>(image_span);
        result.loaded    = type == et_dyn ? kind::dyn : kind::executable;
        result.load_bias = bias;
        result.nsegs     = segment_count;
        mapping.dismiss();
        co_return result;
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
