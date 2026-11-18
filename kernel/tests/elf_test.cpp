#include "test.hpp"
#include <astia/elf.hpp>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>

// End-to-end tests of the generic ELF loader against *real compiler-produced*
// images of both shapes it supports:
//
//   - a relocatable (ET_REL) object, loaded and relocated, with its imported
//     `kernel_add` bound through the resolver and module_main() actually run;
//   - linked executables (ET_DYN PIE and ET_EXEC), with their PT_LOAD segments
//     mapped, a load bias applied, and the R_X86_64_RELATIVE dynamic
//     relocation fixed up, then their entry point actually run.
//
// All three fixtures are compiled and byte-embedded at build time, so the
// loader sees genuine gcc output rather than hand-written ELF.

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// --- ET_REL fixture: imports kernel_add (see tests/hello_mod.c) ---
extern unsigned char hello_mod_blob[];
extern unsigned int  hello_mod_blob_len;

// --- Executable fixtures (see tests/fixture_exec.c) ---
extern unsigned char fixture_dyn_blob[];
extern unsigned int  fixture_dyn_blob_len;
extern unsigned char fixture_exec_blob[];
extern unsigned int  fixture_exec_blob_len;

extern "C" int kernel_add(int a, int b) {
    return a + b;
}

namespace kernel_fixture {

    int add(int left, int right) {
        return left + right;
    }

    int bias() {
        return 0;
    }

}

namespace {

    // ====================================================================
    //  ET_REL environment: a bump arena for sections, with import stubs
    //  grown down from the top. The stub lets the module's PLT32 call to
    //  the host `kernel_add` stay within signed-32-bit range regardless of
    //  where the host function actually lives (mirrors a PLT/GOT stub).
    // ====================================================================

    struct stub_entry {
        const char* name;
        astia::u64  addr;
    };

    struct reloc_arena {
        unsigned char* base;
        size_t         capacity;
        size_t         front; // sections grow up
        size_t         back;  // import stubs grow down
        stub_entry     cache[8];
        size_t         cache_n;
        size_t         mapped;
        size_t         unmapped;
    };

    void* reloc_map(void* ctx, astia::u64, astia::usize size, astia::usize align,
                    bool fixed) {
        if (fixed) {
            return nullptr; // relocatable images are never fixed-address
        }
        auto* a = static_cast<reloc_arena*>(ctx);
        if (align == 0) {
            align = 1;
        }
        size_t off = (a->front + align - 1) & ~(align - 1);
        if (off + size > a->back) {
            return nullptr;
        }
        void* p  = a->base + off;
        a->front = off + size;
        ++a->mapped;
        return p;
    }

    void reloc_unmap(void* ctx, void*, astia::usize) {
        ++static_cast<reloc_arena*>(ctx)->unmapped;
    }

    astia::u64 reloc_resolve(void* ctx, const char* name) {
        auto* a = static_cast<reloc_arena*>(ctx);
        for (size_t i = 0; i < a->cache_n; ++i) {
            if (std::strcmp(a->cache[i].name, name) == 0) {
                return a->cache[i].addr;
            }
        }
        std::uintptr_t target = 0;
        if (std::strcmp(name, "kernel_add") == 0) {
            target = reinterpret_cast<std::uintptr_t>(&kernel_add);
        }
        else if (std::strcmp(name, "_ZN14kernel_fixture3addEii") == 0) {
            target = reinterpret_cast<std::uintptr_t>(&kernel_fixture::add);
        }
        else if (std::strcmp(name, "_ZN14kernel_fixture4biasEv") == 0) {
            target = reinterpret_cast<std::uintptr_t>(&kernel_fixture::bias);
        }
        if (target == 0) {
            return 0;
        }
        if (a->back < a->front + 12) {
            return 0;
        }
        a->back -= 12;
        unsigned char* s = a->base + a->back;
        s[0]             = 0x48; // REX.W
        s[1]             = 0xB8; // mov rax, imm64
        std::memcpy(s + 2, &target, 8);
        s[10] = 0xFF; // jmp rax
        s[11] = 0xE0;

        astia::u64 stub = reinterpret_cast<astia::u64>(s);
        if (a->cache_n < 8) {
            a->cache[a->cache_n++] = {name, stub};
        }
        return stub;
    }

    // ====================================================================
    //  Executable environment: mmap the whole PT_LOAD span. ET_DYN is placed
    //  anywhere; ET_EXEC must land at its fixed link address.
    // ====================================================================

    struct exec_arena {
        void*  base = nullptr;
        size_t span = 0;
        size_t mapped = 0;
        size_t unmapped = 0;
    };

    void* exec_map(void* ctx, astia::u64 hint, astia::usize size, astia::usize,
                   bool fixed) {
        auto* a     = static_cast<exec_arena*>(ctx);
        int   flags = MAP_PRIVATE | MAP_ANONYMOUS | (fixed ? MAP_FIXED_NOREPLACE : 0);
        void* want  = fixed ? reinterpret_cast<void*>(hint) : nullptr;
        void* p     = mmap(want, size, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
        if (p == MAP_FAILED) {
            return nullptr;
        }
        a->base = p;
        a->span = size;
        ++a->mapped;
        return p;
    }

    void exec_unmap(void* ctx, void* address, astia::usize size) {
        auto* a = static_cast<exec_arena*>(ctx);
        munmap(address, size);
        a->base = nullptr;
        a->span = 0;
        ++a->unmapped;
    }

    // Map a fixed-capacity copy of a fixture we can corrupt per test.
    struct blob_copy {
        unsigned char data[16384];
        astia::usize  len;

        blob_copy(const unsigned char* src, astia::usize n) : data{}, len(n) {
            if (len > sizeof(data)) {
                len = sizeof(data);
            }
            std::memcpy(data, src, len);
        }
    };

    unsigned long long blob_u64(const blob_copy& blob, size_t offset) {
        unsigned long long value = 0;
        for (size_t i = 0; i < 8; ++i)
            value |= static_cast<unsigned long long>(blob.data[offset + i]) << (i * 8);
        return value;
    }

    void blob_put_u32(blob_copy& blob, size_t offset, unsigned int value) {
        for (size_t i = 0; i < 4; ++i)
            blob.data[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    }

    void blob_put_u64(blob_copy& blob, size_t offset, unsigned long long value) {
        for (size_t i = 0; i < 8; ++i)
            blob.data[offset + i] = static_cast<unsigned char>(value >> (i * 8));
    }

    size_t first_dynamic_relocation(blob_copy& blob) {
        const auto phoff = static_cast<size_t>(blob_u64(blob, 32));
        const auto phentsize = static_cast<size_t>(blob.data[54]) |
                               (static_cast<size_t>(blob.data[55]) << 8);
        const auto phnum = static_cast<size_t>(blob.data[56]) |
                           (static_cast<size_t>(blob.data[57]) << 8);
        unsigned long long rela_vaddr = 0;
        for (size_t i = 0; i < phnum; ++i) {
            const size_t ph = phoff + i * phentsize;
            const unsigned int type = static_cast<unsigned int>(blob.data[ph]) |
                                      (static_cast<unsigned int>(blob.data[ph + 1]) << 8) |
                                      (static_cast<unsigned int>(blob.data[ph + 2]) << 16) |
                                      (static_cast<unsigned int>(blob.data[ph + 3]) << 24);
            if (type != 2)
                continue;
            const size_t dynamic_offset = static_cast<size_t>(blob_u64(blob, ph + 8));
            const size_t dynamic_size = static_cast<size_t>(blob_u64(blob, ph + 32));
            for (size_t offset = 0; offset < dynamic_size; offset += 16) {
                const size_t entry = dynamic_offset + offset;
                const auto tag = static_cast<long long>(blob_u64(blob, entry));
                if (tag == 0)
                    break;
                if (tag == 7)
                    rela_vaddr = blob_u64(blob, entry + 8);
            }
        }
        for (size_t i = 0; i < phnum; ++i) {
            const size_t ph = phoff + i * phentsize;
            const unsigned int type = static_cast<unsigned int>(blob.data[ph]) |
                                      (static_cast<unsigned int>(blob.data[ph + 1]) << 8) |
                                      (static_cast<unsigned int>(blob.data[ph + 2]) << 16) |
                                      (static_cast<unsigned int>(blob.data[ph + 3]) << 24);
            if (type != 1)
                continue;
            const auto vaddr = blob_u64(blob, ph + 16);
            const auto offset = blob_u64(blob, ph + 8);
            const auto filesz = blob_u64(blob, ph + 32);
            if (rela_vaddr >= vaddr && rela_vaddr - vaddr < filesz)
                return static_cast<size_t>(offset + rela_vaddr - vaddr);
        }
        return 0;
    }

    void shift_first_relocation_target(blob_copy& blob, long long delta) {
        const size_t offset = first_dynamic_relocation(blob);
        const auto target = blob_u64(blob, offset);
        blob_put_u64(blob, offset, static_cast<unsigned long long>(
                                      static_cast<long long>(target) + delta));
    }

    void reject_first_dynamic_relocation(blob_copy& blob) {
        const size_t offset = first_dynamic_relocation(blob);
        blob_put_u32(blob, offset + 8, 1);
    }

    bool copy_failure(void*, astia::u64, astia::span<const astia::u8>) {
        return false;
    }

    void* misfixed_map(void* ctx, astia::u64, astia::usize size, astia::usize align,
                       bool) {
        return exec_map(ctx, 0, size, align, false);
    }

    struct stream_exec_context {
        const unsigned char* source;
        size_t                source_size;
        exec_arena            arena;
        size_t                read_calls = 0;
        size_t                read_bytes = 0;
        size_t                max_read = 0;
        size_t                fail_read_after = ~size_t{0};
        size_t                copy_calls = 0;
        size_t                fail_copy_after = ~size_t{0};
    };

    void* stream_map(void* ctx, astia::u64 hint, astia::usize size, astia::usize align,
                     bool fixed) {
        auto* context = static_cast<stream_exec_context*>(ctx);
        return exec_map(&context->arena, hint, size, align, fixed);
    }

    void stream_unmap(void* ctx, void* address, astia::usize size) {
        auto* context = static_cast<stream_exec_context*>(ctx);
        exec_unmap(&context->arena, address, size);
    }

    astia::core::task<astia::i64> stream_read(void* ctx,
                                               astia::span<astia::u8> destination,
                                               astia::u64 offset) {
        auto* context = static_cast<stream_exec_context*>(ctx);
        if (offset > context->source_size ||
            destination.size() > context->source_size - offset)
            co_return astia::error_t::erange;
        if (context->read_calls >= context->fail_read_after)
            co_return astia::error_t::eio;
        ++context->read_calls;
        context->read_bytes += destination.size();
        if (destination.size() > context->max_read)
            context->max_read = destination.size();
        for (astia::usize i = 0; i < destination.size(); ++i)
            destination[i] = context->source[offset + i];
        co_return static_cast<astia::i64>(destination.size());
    }

    bool stream_copy(void* ctx, astia::u64 address, astia::span<const astia::u8> source) {
        auto* context = static_cast<stream_exec_context*>(ctx);
        if (context->copy_calls >= context->fail_copy_after)
            return false;
        ++context->copy_calls;
        const astia::u64 base = reinterpret_cast<astia::u64>(context->arena.base);
        if (address < base || address - base > context->arena.span ||
            source.size() > context->arena.span - (address - base))
            return false;
        std::memcpy(reinterpret_cast<void*>(address), source.data(), source.size());
        return true;
    }

} // namespace

// ========================================================================
//  Kind probing
// ========================================================================

ASTIA_TEST(elf_identifies_kind) {
    ASTIA_CHECK(astia::elf::kind_of(hello_mod_blob, hello_mod_blob_len) ==
                astia::elf::kind::relocatable);
    ASTIA_CHECK(astia::elf::kind_of(fixture_dyn_blob, fixture_dyn_blob_len) ==
                astia::elf::kind::dyn);
    ASTIA_CHECK(astia::elf::kind_of(fixture_exec_blob, fixture_exec_blob_len) ==
                astia::elf::kind::executable);

    const unsigned char garbage[4] = {0, 1, 2, 3};
    ASTIA_CHECK(astia::elf::kind_of(garbage, sizeof(garbage)) == astia::elf::kind::none);
    ASTIA_CHECK(astia::elf::kind_of(nullptr, 100) == astia::elf::kind::none);
}

// ========================================================================
//  Relocatable (ET_REL): load, relocate, run
// ========================================================================

ASTIA_TEST(elf_loads_and_runs_relocatable_object) {
    const size_t cap = 1 << 20;
    void*        mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASTIA_CHECK(mem != MAP_FAILED);
    if (mem == MAP_FAILED) {
        return;
    }

    reloc_arena a{};
    a.base     = static_cast<unsigned char*>(mem);
    a.capacity = cap;
    a.front    = 0;
    a.back     = cap;
    a.cache_n  = 0;

    astia::elf::env e{};
    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::image img;
    ASTIA_CHECK(astia::elf::load(hello_mod_blob, hello_mod_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::kind::relocatable);
    ASTIA_CHECK(img.entry != nullptr);
    ASTIA_CHECK(img.load_bias == 0);
    ASTIA_CHECK(img.nsegs == 0); // relocatable images have no program segments
    ASTIA_CHECK(a.mapped == 1);
    ASTIA_CHECK(a.unmapped == 0);

    if (img.entry != nullptr) {
        auto entry = reinterpret_cast<int (*)()>(img.entry);
        // counter starts at 40 (.data), module_main does counter += 2 then
        // returns kernel_add(counter, 0) == 42.
        ASTIA_CHECK(entry() == 42);
        ASTIA_CHECK(entry() == 44); // module-local state persists across calls
    }

    munmap(mem, cap);
}

ASTIA_TEST(elf_loads_cpp_module_against_multiple_mangled_symbols) {
    extern unsigned char module_symbols_blob[];
    extern unsigned int module_symbols_blob_len;
    const size_t cap = 1 << 20;
    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASTIA_CHECK(mem != MAP_FAILED);
    if (mem == MAP_FAILED)
        return;

    reloc_arena a{};
    a.base = static_cast<unsigned char*>(mem);
    a.capacity = cap;
    a.back = cap;
    astia::elf::env e{};
    e.ctx = &a;
    e.map = reloc_map;
    e.unmap = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::image img;
    ASTIA_CHECK(astia::elf::load(module_symbols_blob, module_symbols_blob_len, e, img));
    if (img.entry != nullptr)
        ASTIA_CHECK(reinterpret_cast<int (*)()>(img.entry)() == 42);
    munmap(mem, cap);
}

ASTIA_TEST(elf_relocatable_rejects_bad_magic) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);
    buf.data[0] = 0x00;

    reloc_arena     a{};
    astia::elf::env e{};
    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
    ASTIA_CHECK(a.mapped == a.unmapped);
}

ASTIA_TEST(elf_failed_relocatable_loads_release_every_mapping) {
    const size_t cap = 1 << 16;
    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASTIA_CHECK(mem != MAP_FAILED);
    if (mem == MAP_FAILED)
        return;

    reloc_arena a{};
    a.base = static_cast<unsigned char*>(mem);
    a.capacity = cap;
    astia::elf::env e{};
    e.ctx = &a;
    e.map = reloc_map;
    e.unmap = reloc_unmap;
    e.resolve = nullptr;
    for (size_t i = 0; i < 32; ++i) {
        a.front = 0;
        a.back = cap;
        a.cache_n = 0;
        astia::elf::image img;
        ASTIA_CHECK(!astia::elf::load(hello_mod_blob, hello_mod_blob_len, e, img));
        ASTIA_CHECK(a.mapped == a.unmapped);
    }
    ASTIA_CHECK(a.mapped == 32);
    ASTIA_CHECK(a.unmapped == 32);
    munmap(mem, cap);
}

ASTIA_TEST(elf_relocatable_rejects_wrong_machine) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);
    buf.data[18] = 0x00; // e_machine (u16 LE); EM_X86_64 is 62
    buf.data[19] = 0x00;

    reloc_arena     a{};
    astia::elf::env e{};
    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_non_relocatable) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);
    buf.data[16] = 2; // e_type (u16 LE); set ET_EXEC instead of ET_REL
    buf.data[17] = 0;

    reloc_arena     a{};
    astia::elf::env e{};
    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::image img;
    // Now it identifies as an executable and takes the executable path, which
    // has no PT_LOAD / module_main to satisfy.
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_unresolved_symbol) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    reloc_arena     a{};
    astia::elf::env e{};
    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = nullptr; // no resolver: kernel_add cannot bind

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_unterminated_defined_symbol_name) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);
    const auto read_u16 = [&buf](size_t offset) {
        return static_cast<unsigned short>(buf.data[offset]) |
               (static_cast<unsigned short>(buf.data[offset + 1]) << 8);
    };
    const auto read_u32 = [&buf](size_t offset) {
        return static_cast<unsigned int>(buf.data[offset]) |
               (static_cast<unsigned int>(buf.data[offset + 1]) << 8) |
               (static_cast<unsigned int>(buf.data[offset + 2]) << 16) |
               (static_cast<unsigned int>(buf.data[offset + 3]) << 24);
    };
    const auto read_u64 = [&buf](size_t offset) {
        unsigned long long value = 0;
        for (size_t i = 0; i < 8; ++i)
            value |= static_cast<unsigned long long>(buf.data[offset + i]) << (i * 8);
        return value;
    };
    const unsigned long long section_offset = read_u64(40);
    const unsigned short section_count = read_u16(60);
    const unsigned short section_size = read_u16(58);
    size_t string_offset = 0;
    size_t string_size = 0;
    for (unsigned short i = 0; i < section_count; ++i) {
        const size_t section = static_cast<size_t>(section_offset) +
                               static_cast<size_t>(i) * section_size;
        if (read_u32(section + 4) != 2)
            continue;
        const unsigned int string_index = read_u32(section + 40);
        const size_t string_section = static_cast<size_t>(section_offset) +
                                      static_cast<size_t>(string_index) * section_size;
        string_offset = static_cast<size_t>(read_u64(string_section + 24));
        string_size = static_cast<size_t>(read_u64(string_section + 32));
        break;
    }
    ASTIA_CHECK(string_size != 0);
    if (string_size != 0) {
        size_t name_offset = string_size;
        for (size_t i = 0; i + sizeof("module_main") <= string_size; ++i) {
            if (std::memcmp(buf.data + string_offset + i, "module_main",
                            sizeof("module_main") - 1) == 0 &&
                buf.data[string_offset + i + sizeof("module_main") - 1] == '\0') {
                name_offset = i;
                break;
            }
        }
        ASTIA_CHECK(name_offset < string_size);
        if (name_offset < string_size)
            buf.data[string_offset + name_offset + sizeof("module_main") - 1] = 'x';
        reloc_arena a{};
        astia::elf::env e{};
        e.ctx = &a;
        e.map = reloc_map;
        e.unmap = reloc_unmap;
        e.resolve = reloc_resolve;
        astia::elf::image img;
        ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
    }
}

ASTIA_TEST(elf_relocatable_rejects_bad_alignment_and_table_sizes) {
    auto load_bad = [](blob_copy& buf) {
        unsigned char storage[1 << 16]{};
        reloc_arena arena{};
        arena.base = storage;
        arena.capacity = sizeof(storage);
        arena.back = sizeof(storage);
        astia::elf::env environment{};
        environment.ctx = &arena;
        environment.map = reloc_map;
        environment.unmap = reloc_unmap;
        environment.resolve = reloc_resolve;
        astia::elf::image image;
        const bool loaded = astia::elf::load(buf.data, buf.len, environment, image);
        if (loaded)
            reloc_unmap(&arena, image.base, image.size);
        return loaded;
    };

    blob_copy zero_alignment(hello_mod_blob, hello_mod_blob_len);
    const size_t section_offset = static_cast<size_t>(blob_u64(zero_alignment, 40));
    const size_t section_size = static_cast<size_t>(zero_alignment.data[58]) |
                                (static_cast<size_t>(zero_alignment.data[59]) << 8);
    blob_put_u64(zero_alignment, section_offset + section_size * 4 + 48, 0);
    ASTIA_CHECK(load_bad(zero_alignment));

    blob_copy bad_alignment(hello_mod_blob, hello_mod_blob_len);
    blob_put_u64(bad_alignment, section_offset + section_size + 48, 3);
    ASTIA_CHECK(!load_bad(bad_alignment));

    blob_copy bad_symbol_size(hello_mod_blob, hello_mod_blob_len);
    const size_t section_count = static_cast<size_t>(bad_symbol_size.data[60]) |
                                 (static_cast<size_t>(bad_symbol_size.data[61]) << 8);
    for (size_t i = 0; i < section_count;
         ++i) {
        const size_t section = section_offset + i * section_size;
        const unsigned int type = static_cast<unsigned int>(bad_symbol_size.data[section + 4]) |
                                  (static_cast<unsigned int>(bad_symbol_size.data[section + 5])
                                   << 8) |
                                  (static_cast<unsigned int>(bad_symbol_size.data[section + 6])
                                   << 16) |
                                  (static_cast<unsigned int>(bad_symbol_size.data[section + 7])
                                   << 24);
        if (type != 2)
            continue;
        const auto size = blob_u64(bad_symbol_size, section + 32);
        blob_put_u64(bad_symbol_size, section + 32, size - 1);
        break;
    }
    ASTIA_CHECK(!load_bad(bad_symbol_size));

    blob_copy bad_rela_size(hello_mod_blob, hello_mod_blob_len);
    for (size_t i = 0; i < section_count;
         ++i) {
        const size_t section = section_offset + i * section_size;
        const unsigned int type = static_cast<unsigned int>(bad_rela_size.data[section + 4]) |
                                  (static_cast<unsigned int>(bad_rela_size.data[section + 5])
                                   << 8) |
                                  (static_cast<unsigned int>(bad_rela_size.data[section + 6])
                                   << 16) |
                                  (static_cast<unsigned int>(bad_rela_size.data[section + 7])
                                   << 24);
        if (type != 4)
            continue;
        const auto size = blob_u64(bad_rela_size, section + 32);
        blob_put_u64(bad_rela_size, section + 32, size - 1);
        break;
    }
    ASTIA_CHECK(!load_bad(bad_rela_size));

}

// ========================================================================
//  Linked executables (ET_DYN PIE and ET_EXEC): map segments, bias, run
// ========================================================================

ASTIA_TEST(elf_loads_and_runs_pie_executable) {
    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;

    astia::elf::image img;
    ASTIA_CHECK(astia::elf::load(fixture_dyn_blob, fixture_dyn_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::kind::dyn);
    ASTIA_CHECK(img.load_bias != 0); // a PIE is placed away from its link address
    ASTIA_CHECK(img.nsegs >= 1);
    ASTIA_CHECK(img.entry != nullptr);

    for (astia::usize i = 0; i < img.nsegs; ++i) {
        auto want = reinterpret_cast<void*>(img.segs[i].vaddr + img.load_bias);
        ASTIA_CHECK(img.segs[i].loc == want);
    }

    if (img.entry != nullptr) {
        auto entry = reinterpret_cast<int (*)()>(img.entry);
        // Returns 42 then 44, which also proves the R_X86_64_RELATIVE fixup of
        // the `p = &counter` pointer took effect (otherwise *p would fault).
        ASTIA_CHECK(entry() == 42);
        ASTIA_CHECK(entry() == 44);
    }

    munmap(a.base, a.span);
}

ASTIA_TEST(elf_loads_and_runs_fixed_executable) {
    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;

    astia::elf::image img;
    ASTIA_CHECK(astia::elf::load(fixture_exec_blob, fixture_exec_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::kind::executable);
    ASTIA_CHECK(img.load_bias == 0); // ET_EXEC is linked, not relocated
    ASTIA_CHECK(img.nsegs >= 1);
    ASTIA_CHECK(img.entry != nullptr);

    for (astia::usize i = 0; i < img.nsegs; ++i) {
        auto want = reinterpret_cast<void*>(img.segs[i].vaddr + img.load_bias);
        ASTIA_CHECK(img.segs[i].loc == want);
    }

    if (img.entry != nullptr) {
        auto entry = reinterpret_cast<int (*)()>(img.entry);
        ASTIA_CHECK(entry() == 42);
        ASTIA_CHECK(entry() == 44);
    }

    munmap(a.base, a.span);
}

ASTIA_TEST(elf_nonstream_unaligned_relative_relocation_is_safe) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);
    shift_first_relocation_target(buf, -1);
    exec_arena a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;
    astia::elf::image img;
    ASTIA_CHECK(astia::elf::load(buf.data, buf.len, e, img));
    ASTIA_CHECK(a.mapped == 1);
    ASTIA_CHECK(a.unmapped == 0);
    if (a.base != nullptr)
        munmap(a.base, a.span);
}

ASTIA_TEST(elf_executable_failures_release_mapped_span) {
    blob_copy relocation_failure(fixture_dyn_blob, fixture_dyn_blob_len);
    reject_first_dynamic_relocation(relocation_failure);
    exec_arena relocation_arena{};
    astia::elf::env relocation_env{};
    relocation_env.ctx = &relocation_arena;
    relocation_env.map = exec_map;
    relocation_env.unmap = exec_unmap;
    astia::elf::image relocation_image;
    ASTIA_CHECK(!astia::elf::load(relocation_failure.data, relocation_failure.len,
                                  relocation_env, relocation_image));
    ASTIA_CHECK(relocation_arena.mapped == 1);
    ASTIA_CHECK(relocation_arena.unmapped == 1);

    exec_arena copy_arena{};
    astia::elf::env copy_env{};
    copy_env.ctx = &copy_arena;
    copy_env.map = exec_map;
    copy_env.unmap = exec_unmap;
    copy_env.copy = copy_failure;
    astia::elf::image copy_image;
    ASTIA_CHECK(!astia::elf::load(fixture_dyn_blob, fixture_dyn_blob_len, copy_env,
                                  copy_image));
    ASTIA_CHECK(copy_arena.mapped == 1);
    ASTIA_CHECK(copy_arena.unmapped == 1);

    exec_arena fixed_arena{};
    astia::elf::env fixed_env{};
    fixed_env.ctx = &fixed_arena;
    fixed_env.map = misfixed_map;
    fixed_env.unmap = exec_unmap;
    astia::elf::image fixed_image;
    ASTIA_CHECK(!astia::elf::load(fixture_exec_blob, fixture_exec_blob_len, fixed_env,
                                  fixed_image));
    ASTIA_CHECK(fixed_arena.mapped == 1);
    ASTIA_CHECK(fixed_arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_unaligned_relative_relocation_is_safe) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);
    shift_first_relocation_target(buf, -1);
    stream_exec_context context{buf.data, buf.len, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;
    auto task = astia::elf::load_executable_stream(environment, buf.len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr);
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 0);
    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_uses_bounded_reads) {
    stream_exec_context context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::load_executable_stream(environment, fixture_dyn_blob_len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(context.read_calls > 0);
    ASTIA_CHECK(context.max_read <= 4096);
    ASTIA_CHECK(context.read_bytes < fixture_dyn_blob_len);
    if (!task.failed() && task.value() != nullptr) {
        const auto& image = *task.value();
        const size_t phoff = static_cast<size_t>(blob_u64(
            blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), 32));
        const size_t phnum = static_cast<size_t>(fixture_dyn_blob[56]) |
                             (static_cast<size_t>(fixture_dyn_blob[57]) << 8);
        const size_t phentsize = static_cast<size_t>(fixture_dyn_blob[54]) |
                                 (static_cast<size_t>(fixture_dyn_blob[55]) << 8);
        for (size_t i = 0; i < phnum; ++i) {
            const size_t ph = phoff + i * phentsize;
            const unsigned int type = static_cast<unsigned int>(fixture_dyn_blob[ph]) |
                                      (static_cast<unsigned int>(fixture_dyn_blob[ph + 1]) << 8) |
                                      (static_cast<unsigned int>(fixture_dyn_blob[ph + 2]) << 16) |
                                      (static_cast<unsigned int>(fixture_dyn_blob[ph + 3]) << 24);
            if (type != 1)
                continue;
            const auto vaddr = blob_u64(
                blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 16);
            const auto offset = blob_u64(
                blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 8);
            const auto filesz = blob_u64(
                blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 32);
            bool found = false;
            for (size_t seg = 0; seg < image.nsegs; ++seg) {
                if (image.segs[seg].vaddr != vaddr)
                    continue;
                found = true;
                ASTIA_CHECK(image.segs[seg].filesz == filesz);
                const size_t sample = filesz < 16 ? static_cast<size_t>(filesz) : 16;
                ASTIA_CHECK(std::memcmp(image.segs[seg].loc,
                                        fixture_dyn_blob + offset,
                                        sample) == 0);
            }
            ASTIA_CHECK(found);
        }
    }
    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_rejects_truncated_program_headers) {
    const size_t phoff = static_cast<size_t>(blob_u64(
        blob_copy(fixture_exec_blob, fixture_exec_blob_len), 32));
    const size_t phnum = static_cast<size_t>(fixture_exec_blob[56]) |
                         (static_cast<size_t>(fixture_exec_blob[57]) << 8);
    const size_t truncated_size = phoff + phnum * 56 - 1;
    stream_exec_context context{fixture_exec_blob, truncated_size, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::load_executable_stream(environment, context.source_size);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 0);
}

ASTIA_TEST(elf_stream_rejects_malformed_segment_sizes) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);
    const size_t phoff = static_cast<size_t>(blob_u64(buf, 32));
    const size_t phentsize = static_cast<size_t>(buf.data[54]) |
                             (static_cast<size_t>(buf.data[55]) << 8);
    const size_t phnum = static_cast<size_t>(buf.data[56]) |
                         (static_cast<size_t>(buf.data[57]) << 8);
    bool changed = false;
    for (size_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + i * phentsize;
        const unsigned int type = static_cast<unsigned int>(buf.data[ph]) |
                                  (static_cast<unsigned int>(buf.data[ph + 1]) << 8) |
                                  (static_cast<unsigned int>(buf.data[ph + 2]) << 16) |
                                  (static_cast<unsigned int>(buf.data[ph + 3]) << 24);
        if (type != 1)
            continue;
        const auto filesz = blob_u64(buf, ph + 32);
        blob_put_u64(buf, ph + 40, filesz - 1);
        changed = true;
        break;
    }
    ASTIA_CHECK(changed);

    stream_exec_context context{buf.data, buf.len, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;
    auto task = astia::elf::load_executable_stream(environment, buf.len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 0);
}

ASTIA_TEST(elf_stream_read_failure_releases_mapping) {
    stream_exec_context context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    context.fail_read_after = 3;
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::load_executable_stream(environment, fixture_dyn_blob_len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_copy_failure_releases_mapping) {
    stream_exec_context context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    context.fail_copy_after = 1;
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.copy = stream_copy;
    environment.unmap = stream_unmap;

    auto task = astia::elf::load_executable_stream(environment, fixture_dyn_blob_len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_relocation_failure_releases_mapping) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);
    reject_first_dynamic_relocation(buf);
    stream_exec_context context{buf.data, buf.len, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::load_executable_stream(environment, buf.len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_tls_failure_releases_mapping) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);
    const size_t phoff = static_cast<size_t>(blob_u64(buf, 32));
    const size_t phentsize = static_cast<size_t>(buf.data[54]) |
                             (static_cast<size_t>(buf.data[55]) << 8);
    const size_t phnum = static_cast<size_t>(buf.data[56]) |
                         (static_cast<size_t>(buf.data[57]) << 8);
    bool changed = false;
    for (size_t i = 0; i < phnum; ++i) {
        const size_t ph = phoff + i * phentsize;
        const unsigned int type = static_cast<unsigned int>(buf.data[ph]) |
                                  (static_cast<unsigned int>(buf.data[ph + 1]) << 8) |
                                  (static_cast<unsigned int>(buf.data[ph + 2]) << 16) |
                                  (static_cast<unsigned int>(buf.data[ph + 3]) << 24);
        if (type == 1 || type == 2)
            continue;
        blob_put_u32(buf, ph, 7);
        blob_put_u64(buf, ph + 32, 1);
        blob_put_u64(buf, ph + 40, 0);
        blob_put_u64(buf, ph + 48, 1);
        changed = true;
        break;
    }
    ASTIA_CHECK(changed);

    stream_exec_context context{buf.data, buf.len, {}};
    astia::elf::async_env environment{};
    environment.ctx = &context;
    environment.map = stream_map;
    environment.read = stream_read;
    environment.unmap = stream_unmap;
    auto task = astia::elf::load_executable_stream(environment, buf.len);
    while (!task.done())
        task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_executable_rejects_bad_magic) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);
    buf.data[0] = 0x00;

    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_executable_rejects_wrong_machine) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);
    buf.data[18] = 0x03; // e_machine -> EM_386, not x86-64
    buf.data[19] = 0x00;

    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_executable_rejects_truncated_phdrs) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;
    e.unmap = exec_unmap;

    astia::elf::image img;
    // Shorter than the program-header table the header points at.
    ASTIA_CHECK(!astia::elf::load(buf.data, 60, e, img));
}
