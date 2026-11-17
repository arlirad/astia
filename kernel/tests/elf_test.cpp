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
        return p;
    }

    // Map a fixed-capacity copy of a fixture we can corrupt per test.
    struct blob_copy {
        unsigned char data[8192];
        astia::usize  len;

        blob_copy(const unsigned char* src, astia::usize n) : data{}, len(n) {
            if (len > sizeof(data)) {
                len = sizeof(data);
            }
            std::memcpy(data, src, len);
        }
    };

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

// ========================================================================
//  Linked executables (ET_DYN PIE and ET_EXEC): map segments, bias, run
// ========================================================================

ASTIA_TEST(elf_loads_and_runs_pie_executable) {
    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;

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

ASTIA_TEST(elf_executable_rejects_bad_magic) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);
    buf.data[0] = 0x00;

    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;

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

    astia::elf::image img;
    ASTIA_CHECK(!astia::elf::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_executable_rejects_truncated_phdrs) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    exec_arena      a{};
    astia::elf::env e{};
    e.ctx = &a;
    e.map = exec_map;

    astia::elf::image img;
    // Shorter than the program-header table the header points at.
    ASTIA_CHECK(!astia::elf::load(buf.data, 60, e, img));
}
