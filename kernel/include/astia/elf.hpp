#pragma once

#include <astia/common.hpp>
#include <astia/core/task.hpp>
#include <astia/span.hpp>

// A freestanding x86-64 ELF loader, generic over the two shapes the kernel
// actually consumes:
//
//   - Relocatable objects (ET_REL, `gcc -c`): kernel modules. The loader
//     lays out the SHF_ALLOC sections, resolves undefined symbols against the
//     caller, and applies the section relocations.
//   - Linked executables (ET_EXEC and PIE ET_DYN): userspace programs. The
//     loader maps the PT_LOAD segments, picks a load bias (0 for the fixed
//     ET_EXEC case, allocator-chosen for ET_DYN), applies the R_X86_64_RELATIVE
//     dynamic relocations a PIE needs, and hands back e_entry + bias.
//
// Both paths are policy-free: where memory comes from and how symbols are
// resolved are supplied by the caller as function pointers, so the identical
// code runs against the kernel's frame allocator (identity-mapped) with the
// export table as resolver, and against a host mmap arena in tests.
//
// Out of scope (rejected rather than silently mishandled): x86-64 only, ELF64
// little-endian only, ET_REL uses RELA only, and executables may contain only
// R_X86_64_RELATIVE dynamic relocations — full dynamic linking (GOT/PLT symbol
// binding against a runtime libc) is a separate, larger concern. COMMON symbols
// are likewise rejected.

namespace astia::elf {

    /// ELF identification bytes required by the System V ELF format.
    inline constexpr u8 elf_magic[] = {0x7f, 'E', 'L', 'F'};

    /// Number of bytes in the ELF identification array.
    inline constexpr usize elf_ident_size = 16;

    /// Fixed caps used by the loader's stack-backed metadata tables.
    inline constexpr usize max_sections           = 256;
    inline constexpr usize max_program_headers    = 64;
    inline constexpr usize max_init_array_entries = 64;

    /// What kind of ELF image this is (from the header's e_type, after
    /// validating the ELF64/x86-64 identity).
    enum class kind : u8 {
        none,        ///< Not a supported ELF image.
        relocatable, ///< ET_REL: a linkable object (kernel module).
        executable,  ///< ET_EXEC: a fixed-address executable.
        dyn,         ///< ET_DYN: a PIE / shared object (load-biased).
    };

    /// Page size the executable loader aligns segment spans to (x86-64).
    inline constexpr u64 page_size = mm::page_size;

    /// Map `size` bytes aligned to `align` (a power of two). When `fixed` is
    /// true the region must begin exactly at `hint` (an ET_EXEC's link
    /// address). Otherwise the callback may choose the address, but it must
    /// preserve `hint`'s congruence class modulo `align`; this lets ET_DYN
    /// retain PT_LOAD load-bias congruence without requiring the hint itself
    /// to be aligned to `align`. Returns null on failure.
    using map_fn = void* (*) (void* ctx, u64 hint, usize size, usize align, bool fixed);

    /// Resolve an undefined global symbol by name to its runtime address.
    /// Returns 0 if the name is unknown. (Integer, not pointer, so neither side
    /// has to cast function pointers through object pointers.) Only the
    /// relocatable path consults this.
    using resolve_fn = u64 (*)(void* ctx, const char* name);

    using copy_fn       = bool (*)(void* ctx, u64 address, span<const u8> source);
    using unmap_fn      = void (*)(void* ctx, void* address, usize size);
    using async_read_fn = core::task<i64> (*)(void* ctx, span<u8> destination,
                                              u64 offset);
    /// Read file bytes into the final mapped image backing at `destination`.
    /// Completion must not be reported while the destination is still in use.
    using async_read_to_fn = core::task<i64> (*)(void* ctx, u64 destination, usize length,
                                                 u64 offset);

    /// Caller-supplied environment: where memory comes from and how symbols
    /// are resolved.
    struct env {
        void*      ctx     = nullptr;
        map_fn     map     = nullptr;
        unmap_fn   unmap   = nullptr;
        resolve_fn resolve = nullptr;
        copy_fn    copy    = nullptr;
    };

    struct async_env {
        void*         ctx     = nullptr;
        map_fn        map     = nullptr;
        resolve_fn    resolve = nullptr;
        async_read_fn read    = nullptr;
        copy_fn       copy    = nullptr;
        unmap_fn      unmap   = nullptr;
        /// Optional direct PT_LOAD reader; falls back to `read` plus `copy`.
        async_read_to_fn read_to = nullptr;
    };

    /// One loaded PT_LOAD segment of an executable (relocatable images leave
    /// this list empty).
    struct segment {
        u64   vaddr  = 0;       ///< Link-time virtual address (p_vaddr).
        u64   filesz = 0;       ///< Bytes copied from the file.
        u64   memsz  = 0;       ///< Bytes occupied in memory (>= filesz; tail is zeroed).
        u32   flags  = 0;       ///< PF_{R,W,X} permissions the segment declares.
        void* loc    = nullptr; ///< Runtime address (vaddr + load_bias).
    };

    using init_fn = void (*)();

    /// Upper bound on PT_LOAD segments we track. A typical binary has 4; the
    /// fixed cap keeps the list off the heap, mirroring memory_map / init.
    inline constexpr usize max_segments = 8;

    /// A successfully loaded image, common to both loaders.
    struct image {
        void* entry = nullptr; ///< Entry point (module_main for ET_REL, e_entry + bias
                               ///< for executables).
        void* base = nullptr;  ///< Base of the loaded allocation (lowest mapped address).
        usize size = 0;        ///< Total bytes spanned by the image.
        kind  loaded = kind::none; ///< Which loader produced this.
        u64   load_bias =
            0; ///< Relocation bias (0 for ET_REL and ET_EXEC; chosen for ET_DYN).
        usize   nsegs              = 0; ///< PT_LOAD segment count (executables only).
        segment segs[max_segments] = {};
        init_fn init_array[max_init_array_entries] = {};
        usize   init_count                         = 0;
        init_fn fini_array[max_init_array_entries] = {};
        usize   fini_count                         = 0;

        // Static TLS template (PT_TLS), materialized per thread by the
        // consumer of this image: file offset and sizes of [tdata][tbss],
        // plus the alignment the thread pointer is derived from.
        u64 tls_offset = 0;
        u64 tls_filesz = 0;
        u64 tls_memsz  = 0;
        u64 tls_align  = 0;
    };

    /// Identify an ELF image's kind from its header, validating the ELF64
    /// little-endian x86-64 identity. Returns kind::none for anything else.
    kind        kind_of(span<const u8> bytes);
    inline kind kind_of(const void* bytes, usize len) {
        return kind_of(span<const u8>(static_cast<const u8*>(bytes), len));
    }

    /// Load a relocatable (ET_REL) object: lay out its SHF_ALLOC sections in
    /// one contiguous block, resolve undefined symbols through `env::resolve`,
    /// apply the section relocations, and set `entry` to the defined
    /// `module_main` symbol. Requires `env::map` and `env::unmap`; failed
    /// loads release the mapped block, while successful loads transfer
    /// ownership to the caller.
    bool        load_relocatable(span<const u8> bytes, const env& e, image& out);
    inline bool load_relocatable(const void* bytes, usize len, const env& e, image& out) {
        return load_relocatable(span<const u8>(static_cast<const u8*>(bytes), len), e,
                                out);
    }

    /// Load a linked executable (ET_EXEC or ET_DYN): map the PT_LOAD segments
    /// (one contiguous span, at a fixed address for ET_EXEC or an
    /// allocator-chosen one for ET_DYN), zero the bss, apply the
    /// R_X86_64_RELATIVE dynamic relocations, and set `entry` to e_entry +
    /// load_bias. Requires `env::map` and `env::unmap`; failed loads release
    /// the mapped span, while successful loads transfer ownership to the
    /// caller.
    bool        load_executable(span<const u8> bytes, const env& e, image& out);
    inline bool load_executable(const void* bytes, usize len, const env& e, image& out) {
        return load_executable(span<const u8>(static_cast<const u8*>(bytes), len), e,
                               out);
    }

    /// Streaming executable loader. Reads only the ELF header/PHDR table and
    /// then fetches PT_LOAD file ranges through the async environment.
    /// The callback context must remain valid until the returned task is done.
    /// The callback table itself is copied into the coroutine frame.
    core::task<image> load_executable_stream(async_env e, usize file_size);

    /// Dispatch to the right loader based on kind_of(). Returns false for
    /// anything that is not a supported ELF image.
    bool        load(span<const u8> bytes, const env& e, image& out);
    inline bool load(const void* bytes, usize len, const env& e, image& out) {
        return load(span<const u8>(static_cast<const u8*>(bytes), len), e, out);
    }

} // namespace astia::elf
