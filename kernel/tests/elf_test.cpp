#include <astia/core/resume_queue.hpp>
#include <astia/core/task.hpp>
#include <astia/elf.hpp>

#include "test.hpp"
#include <sys/mman.h>

#include <coroutine>
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

// --- ET_REL fixture: imports kernel_add (see tests/hello_mod.cpp) ---
extern unsigned char hello_mod_blob[];
extern unsigned int  hello_mod_blob_len;
extern unsigned char module_symbols_blob[];
extern unsigned int  module_symbols_blob_len;
extern unsigned char module_priority_malformed_blob[];
extern unsigned int  module_priority_malformed_blob_len;
extern unsigned char module_priority_overflow_blob[];
extern unsigned int  module_priority_overflow_blob_len;
extern unsigned char module_priority_init_trailing_dot_blob[];
extern unsigned int  module_priority_init_trailing_dot_blob_len;
extern unsigned char module_priority_fini_trailing_dot_blob[];
extern unsigned int  module_priority_fini_trailing_dot_blob_len;
extern unsigned char module_lifecycle_reloc32_blob[];
extern unsigned int  module_lifecycle_reloc32_blob_len;
extern unsigned char module_sync_entry_blob[];
extern unsigned int  module_sync_entry_blob_len;

// --- Executable fixtures (see tests/fixture_exec.c) ---
extern unsigned char fixture_dyn_blob[];
extern unsigned int  fixture_dyn_blob_len;
extern unsigned char fixture_exec_blob[];
extern unsigned int  fixture_exec_blob_len;

#if defined(__clang__)
#define ASTIA_NO_SANITIZE_FUNCTION __attribute__((no_sanitize("function")))
#else
#define ASTIA_NO_SANITIZE_FUNCTION
#endif

int kernel_add(int a, int b) {
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
    ASTIA_NO_SANITIZE_FUNCTION astia::error_t invoke_module_entry(void* entry) {
        using entry_fn = astia::core::task<void> (*)();
        auto task      = reinterpret_cast<entry_fn>(entry)();

        while (!task.done()) {
            task.resume();
        }

        return task.error();
    }

    ASTIA_NO_SANITIZE_FUNCTION void
    invoke_module_initializer(astia::elf::loader::init_fn fn) {
        fn();
    }

    ASTIA_NO_SANITIZE_FUNCTION void
    invoke_module_finalizer(astia::elf::loader::init_fn fn) {
        fn();
    }

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

        if (align == 0)
            align = 1;

        size_t off = (a->front + align - 1) & ~(align - 1);

        if (off + size > a->back)
            return nullptr;

        void* p = a->base + off;

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
            if (std::strcmp(a->cache[i].name, name) == 0)
                return a->cache[i].addr;
        }

        std::uintptr_t target = 0;

        if (std::strcmp(name, "_Z10kernel_addii") == 0)
            target = reinterpret_cast<std::uintptr_t>(&kernel_add);
        else if (std::strcmp(name, "_ZN14kernel_fixture3addEii") == 0)
            target = reinterpret_cast<std::uintptr_t>(&kernel_fixture::add);
        else if (std::strcmp(name, "_ZN14kernel_fixture4biasEv") == 0)
            target = reinterpret_cast<std::uintptr_t>(&kernel_fixture::bias);
        else if (std::strcmp(name, "_Znwm") == 0)
            target = reinterpret_cast<std::uintptr_t>(
                static_cast<void* (*) (astia::usize)>(&::operator new));
        else if (std::strcmp(name, "_ZdlPv") == 0)
            target = reinterpret_cast<std::uintptr_t>(
                static_cast<void (*)(void*)>(&::operator delete));
        else if (std::strcmp(name, "_ZN5astia4core13cancel_resumeENSt7__n486116"
                                   "coroutine_handleIvEEy") == 0)
            target = reinterpret_cast<std::uintptr_t>(&astia::core::cancel_resume);

        if (target == 0)
            return 0;

        if (a->back < a->front + 12)
            return 0;

        a->back -= 12;

        unsigned char* s = a->base + a->back;

        s[0] = 0x48; // REX.W
        s[1] = 0xB8; // mov rax, imm64

        std::memcpy(s + 2, &target, 8);

        s[10] = 0xFF; // jmp rax
        s[11] = 0xE0;

        astia::u64 stub = reinterpret_cast<astia::u64>(s);

        if (a->cache_n < 8)
            a->cache[a->cache_n++] = {name, stub};

        return stub;
    }

    // ====================================================================
    //  Executable environment: mmap the whole PT_LOAD span. ET_DYN is placed
    //  anywhere; ET_EXEC must land at its fixed link address.
    // ====================================================================

    struct exec_arena {
        void*  base     = nullptr;
        size_t span     = 0;
        size_t mapped   = 0;
        size_t unmapped = 0;
    };

    void* exec_map(void* ctx, astia::u64 hint, astia::usize size, astia::usize,
                   bool fixed) {
        auto* a = static_cast<exec_arena*>(ctx);

        int flags = MAP_PRIVATE | MAP_ANONYMOUS | (fixed ? MAP_FIXED_NOREPLACE : 0);

        void* want = fixed ? reinterpret_cast<void*>(hint) : nullptr;
        void* p    = mmap(want, size, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
        if (p == MAP_FAILED)
            return nullptr;

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

    void* congruent_exec_map(void* ctx, astia::u64 hint, astia::usize size,
                             astia::usize align, bool fixed) {
        auto* arena = static_cast<exec_arena*>(ctx);

        if (fixed) {
            void* p = mmap(reinterpret_cast<void*>(hint), size,
                           PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
            if (p == MAP_FAILED)
                return nullptr;

            arena->base = p;
            arena->span = size;

            ++arena->mapped;

            return p;
        }

        const size_t reserve_size = size + align - 1;

        void* reservation =
            mmap(nullptr, reserve_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reservation == MAP_FAILED)
            return nullptr;

        const auto raw       = reinterpret_cast<astia::u64>(reservation);
        const auto candidate = raw + ((hint - raw) & (align - 1));
        const auto prefix    = candidate - raw;
        const auto suffix    = reserve_size - prefix - size;

        if (prefix != 0)
            munmap(reservation, prefix);

        if (suffix != 0)
            munmap(reinterpret_cast<void*>(candidate + size), suffix);

        void* p = reinterpret_cast<void*>(candidate);

        arena->base = p;
        arena->span = size;

        ++arena->mapped;

        return p;
    }

    // Map a fixed-capacity copy of a fixture we can corrupt per test.
    struct blob_copy {
        unsigned char data[65536];
        astia::usize  len;

        blob_copy(const unsigned char* src, astia::usize n) : data{}, len(n) {
            if (len > sizeof(data))
                len = sizeof(data);

            std::memcpy(data, src, len);
        }
    };

    unsigned long long blob_u64(const blob_copy& blob, size_t offset) {
        unsigned long long value = 0;

        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<unsigned long long>(blob.data[offset + i]) << (i * 8);
        }

        return value;
    }

    size_t blob_u32(const blob_copy& blob, size_t offset) {
        return static_cast<size_t>(blob.data[offset]) |
               (static_cast<size_t>(blob.data[offset + 1]) << 8) |
               (static_cast<size_t>(blob.data[offset + 2]) << 16) |
               (static_cast<size_t>(blob.data[offset + 3]) << 24);
    }

    void blob_put_u32(blob_copy& blob, size_t offset, unsigned int value) {
        for (size_t i = 0; i < 4; ++i) {
            blob.data[offset + i] = static_cast<unsigned char>(value >> (i * 8));
        }
    }

    void blob_put_u16(blob_copy& blob, size_t offset, unsigned short value) {
        blob.data[offset]     = static_cast<unsigned char>(value);
        blob.data[offset + 1] = static_cast<unsigned char>(value >> 8);
    }

    void blob_put_u64(blob_copy& blob, size_t offset, unsigned long long value) {
        for (size_t i = 0; i < 8; ++i) {
            blob.data[offset + i] = static_cast<unsigned char>(value >> (i * 8));
        }
    }

    size_t blob_u16(const blob_copy& blob, size_t offset) {
        return static_cast<size_t>(blob.data[offset]) |
               (static_cast<size_t>(blob.data[offset + 1]) << 8);
    }

    size_t section_table_offset(const blob_copy& blob) {
        return static_cast<size_t>(blob_u64(blob, 40));
    }

    size_t section_table_size(const blob_copy& blob) {
        return blob_u16(blob, 58);
    }

    size_t section_count(const blob_copy& blob) {
        return blob_u16(blob, 60);
    }

    size_t section_header_offset(const blob_copy& blob, size_t index) {
        return section_table_offset(blob) + index * section_table_size(blob);
    }

    size_t first_allocated_relocation_info_offset(const blob_copy& blob) {
        for (size_t i = 0; i < section_count(blob); ++i) {
            const size_t section = section_header_offset(blob, i);

            if (blob_u32(blob, section + 4) != 4)
                continue;

            const size_t target = blob_u32(blob, section + 44);

            if (target >= section_count(blob))
                continue;

            if ((blob_u64(blob, section_header_offset(blob, target) + 8) & 2) == 0)
                continue;

            if (blob_u64(blob, section + 32) < 24)
                continue;

            return static_cast<size_t>(blob_u64(blob, section + 24)) + 8;
        }

        return 0;
    }

    size_t module_symbol_offset(const blob_copy& blob, size_t* string_name = nullptr) {
        for (size_t i = 0; i < section_count(blob); ++i) {
            const size_t       section = section_header_offset(blob, i);
            const unsigned int type =
                static_cast<unsigned int>(blob_u32(blob, section + 4));
            if (type != 2)
                continue;

            const size_t symbols = static_cast<size_t>(blob_u64(blob, section + 32)) / 24;
            const size_t symbol_offset =
                static_cast<size_t>(blob_u64(blob, section + 24));
            const size_t string_index   = blob_u32(blob, section + 40);
            const size_t string_section = section_header_offset(blob, string_index);
            const size_t string_offset =
                static_cast<size_t>(blob_u64(blob, string_section + 24));
            const size_t string_size =
                static_cast<size_t>(blob_u64(blob, string_section + 32));

            for (size_t j = 0; j < symbols; ++j) {
                const size_t symbol = symbol_offset + j * 24;
                const size_t name   = static_cast<size_t>(blob.data[symbol]) |
                                    (static_cast<size_t>(blob.data[symbol + 1]) << 8) |
                                    (static_cast<size_t>(blob.data[symbol + 2]) << 16) |
                                    (static_cast<size_t>(blob.data[symbol + 3]) << 24);

                if (name >= string_size ||
                    std::strcmp(
                        reinterpret_cast<const char*>(blob.data + string_offset + name),
                        "_Z11module_mainv") != 0)
                    continue;

                if (string_name != nullptr)
                    *string_name = string_offset + name;

                return symbol;
            }
        }

        return 0;
    }

    bool load_relocatable_fixture(blob_copy& blob) {
        unsigned char storage[1 << 16]{};

        reloc_arena arena{};

        arena.base     = storage;
        arena.capacity = sizeof(storage);
        arena.back     = sizeof(storage);

        astia::elf::loader::env environment{};

        environment.ctx     = &arena;
        environment.map     = reloc_map;
        environment.unmap   = reloc_unmap;
        environment.resolve = reloc_resolve;

        astia::elf::loader::image image;

        return astia::elf::loader::load(blob.data, blob.len, environment, image);
    }

    size_t first_dynamic_relocation(blob_copy& blob) {
        const auto phoff     = static_cast<size_t>(blob_u64(blob, 32));
        const auto phentsize = static_cast<size_t>(blob.data[54]) |
                               (static_cast<size_t>(blob.data[55]) << 8);
        const auto phnum = static_cast<size_t>(blob.data[56]) |
                           (static_cast<size_t>(blob.data[57]) << 8);

        unsigned long long rela_vaddr = 0;

        for (size_t i = 0; i < phnum; ++i) {
            const size_t       ph = phoff + i * phentsize;
            const unsigned int type =
                static_cast<unsigned int>(blob.data[ph]) |
                (static_cast<unsigned int>(blob.data[ph + 1]) << 8) |
                (static_cast<unsigned int>(blob.data[ph + 2]) << 16) |
                (static_cast<unsigned int>(blob.data[ph + 3]) << 24);
            if (type != 2)
                continue;

            const size_t dynamic_offset = static_cast<size_t>(blob_u64(blob, ph + 8));
            const size_t dynamic_size   = static_cast<size_t>(blob_u64(blob, ph + 32));

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
            const size_t       ph = phoff + i * phentsize;
            const unsigned int type =
                static_cast<unsigned int>(blob.data[ph]) |
                (static_cast<unsigned int>(blob.data[ph + 1]) << 8) |
                (static_cast<unsigned int>(blob.data[ph + 2]) << 16) |
                (static_cast<unsigned int>(blob.data[ph + 3]) << 24);
            if (type != 1)
                continue;

            const auto vaddr  = blob_u64(blob, ph + 16);
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

        blob_put_u64(
            blob, offset,
            static_cast<unsigned long long>(static_cast<long long>(target) + delta));
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
        size_t               source_size;
        exec_arena           arena;
        size_t               read_calls             = 0;
        size_t               read_bytes             = 0;
        size_t               max_read               = 0;
        size_t               fail_read_after        = ~size_t{0};
        size_t               direct_reads           = 0;
        size_t               copy_calls             = 0;
        size_t               fail_copy_after        = ~size_t{0};
        bool                 pause_next_read        = false;
        bool                 pause_next_direct_read = false;
        bool                 direct_paused          = false;
        bool                 paused                 = false;
    };

    void* congruent_stream_map(void* ctx, astia::u64 hint, astia::usize size,
                               astia::usize align, bool fixed) {
        auto* context = static_cast<stream_exec_context*>(ctx);

        return congruent_exec_map(&context->arena, hint, size, align, fixed);
    }

    blob_copy make_nonpage_aligned_executable(bool fixed, bool congruent) {
        blob_copy blob(fixture_exec_blob, fixture_exec_blob_len);

        for (size_t i = 0; i < 0x3020; ++i) {
            blob.data[i] = 0;
        }

        blob.len      = 0x3020;
        blob.data[0]  = 0x7f;
        blob.data[1]  = 'E';
        blob.data[2]  = 'L';
        blob.data[3]  = 'F';
        blob.data[4]  = 2;
        blob.data[5]  = 1;
        blob.data[16] = fixed ? 2 : 3;
        blob.data[18] = 62;

        blob_put_u32(blob, 20, 1);

        const astia::u64 vaddr = fixed ? 0x10003000ull : 0x3000ull;

        blob_put_u64(blob, 24, vaddr);
        blob_put_u64(blob, 32, 64);
        blob_put_u64(blob, 40, 0);

        blob.data[52] = 64;
        blob.data[54] = 56;
        blob.data[56] = 1;

        blob_put_u32(blob, 64, 1);
        blob_put_u32(blob, 68, 5);
        blob_put_u64(blob, 72, 0x3000);
        blob_put_u64(blob, 80, vaddr);
        blob_put_u64(blob, 96, 1);
        blob_put_u64(blob, 104, 0x100);
        blob_put_u64(blob, 112, 0x2000);

        blob.data[0x3000] = 0xc3;

        if (!congruent)
            blob_put_u64(blob, 72, 0x2000);

        return blob;
    }

    blob_copy make_stream_boundary_executable() {
        blob_copy blob = make_nonpage_aligned_executable(false, true);

        blob.len = 0x3100;

        for (size_t i = 0x1000; i < blob.len; ++i) {
            blob.data[i] = 0;
        }

        blob_put_u64(blob, 24, 0x1000);
        blob_put_u64(blob, 72, 0x1000);
        blob_put_u64(blob, 80, 0x1000);
        blob_put_u64(blob, 112, 0x1000);
        blob_put_u64(blob, 96, 0x2100);
        blob_put_u64(blob, 104, 0x3000);

        for (size_t i = 0; i < 0x2100; ++i) {
            blob.data[0x1000 + i] = static_cast<unsigned char>(i * 37u + 11u);
        }

        return blob;
    }

    void* stream_map(void* ctx, astia::u64 hint, astia::usize size, astia::usize align,
                     bool fixed) {
        auto* context = static_cast<stream_exec_context*>(ctx);

        return exec_map(&context->arena, hint, size, align, fixed);
    }

    void* pausing_stream_map(void* ctx, astia::u64 hint, astia::usize size,
                             astia::usize align, bool fixed) {
        auto* context = static_cast<stream_exec_context*>(ctx);
        void* result  = stream_map(ctx, hint, size, align, fixed);
        if (result != nullptr)
            context->pause_next_read = true;

        return result;
    }

    void stream_unmap(void* ctx, void* address, astia::usize size) {
        auto* context = static_cast<stream_exec_context*>(ctx);

        exec_unmap(&context->arena, address, size);
    }

    astia::core::task<astia::i64>
    stream_read(void* ctx, astia::span<astia::u8> destination, astia::u64 offset) {
        auto* context = static_cast<stream_exec_context*>(ctx);

        if (offset > context->source_size ||
            destination.size() > context->source_size - offset)
            co_return astia::error_t::erange;

        if (context->read_calls >= context->fail_read_after)
            co_return astia::error_t::eio;

        if (context->pause_next_read) {
            context->pause_next_read = false;
            context->paused          = true;

            co_await std::suspend_always{};

            context->paused = false;
        }

        ++context->read_calls;

        context->read_bytes += destination.size();

        if (destination.size() > context->max_read)
            context->max_read = destination.size();

        for (astia::usize i = 0; i < destination.size(); ++i) {
            destination[i] = context->source[offset + i];
        }

        co_return static_cast<astia::i64>(destination.size());
    }

    astia::core::task<astia::i64> stream_read_to(void* ctx, astia::u64 address,
                                                 astia::usize length, astia::u64 offset) {
        auto* context = static_cast<stream_exec_context*>(ctx);

        if (offset > context->source_size || length > context->source_size - offset)
            co_return astia::error_t::erange;

        if (context->read_calls >= context->fail_read_after)
            co_return astia::error_t::eio;

        if (context->pause_next_direct_read) {
            context->pause_next_direct_read = false;
            context->direct_paused          = true;

            co_await std::suspend_always{};

            context->direct_paused = false;
        }

        const astia::u64 base = reinterpret_cast<astia::u64>(context->arena.base);

        if (address < base || address - base > context->arena.span ||
            length > context->arena.span - (address - base))
            co_return astia::error_t::efault;

        ++context->read_calls;
        ++context->direct_reads;

        context->read_bytes += length;

        if (length > context->max_read)
            context->max_read = length;

        std::memcpy(reinterpret_cast<void*>(address), context->source + offset, length);

        co_return static_cast<astia::i64>(length);
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
    ASTIA_CHECK(astia::elf::loader::kind_of(hello_mod_blob, hello_mod_blob_len) ==
                astia::elf::loader::kind::relocatable);
    ASTIA_CHECK(astia::elf::loader::kind_of(fixture_dyn_blob, fixture_dyn_blob_len) ==
                astia::elf::loader::kind::dyn);
    ASTIA_CHECK(astia::elf::loader::kind_of(fixture_exec_blob, fixture_exec_blob_len) ==
                astia::elf::loader::kind::executable);

    const unsigned char garbage[4] = {0, 1, 2, 3};

    ASTIA_CHECK(astia::elf::loader::kind_of(garbage, sizeof(garbage)) ==
                astia::elf::loader::kind::none);
    ASTIA_CHECK(astia::elf::loader::kind_of(nullptr, 100) ==
                astia::elf::loader::kind::none);
}

// ========================================================================
//  Relocatable (ET_REL): load, relocate, run
// ========================================================================

ASTIA_TEST(elf_loads_and_runs_relocatable_object) {
    const size_t cap = 1 << 20;

    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    ASTIA_CHECK(mem != MAP_FAILED);

    if (mem == MAP_FAILED)
        return;

    reloc_arena a{};

    a.base     = static_cast<unsigned char*>(mem);
    a.capacity = cap;
    a.front    = 0;
    a.back     = cap;
    a.cache_n  = 0;

    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::loader::image img;

    ASTIA_CHECK(astia::elf::loader::load(hello_mod_blob, hello_mod_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::loader::kind::relocatable);
    ASTIA_CHECK(img.entry != nullptr);
    ASTIA_CHECK(img.load_bias == 0);
    ASTIA_CHECK(img.nsegs == 0); // relocatable images have no program segments
    ASTIA_CHECK(a.mapped == 1);
    ASTIA_CHECK(a.unmapped == 0);

    if (img.entry != nullptr) {
        ASTIA_CHECK(invoke_module_entry(img.entry) == astia::error_t::enone);
        ASTIA_CHECK(invoke_module_entry(img.entry) == astia::error_t::enone);
    }

    munmap(mem, cap);
}

ASTIA_TEST(elf_loads_cpp_module_against_multiple_mangled_symbols) {
    const size_t cap = 1 << 20;

    void* mem = mmap(nullptr, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    ASTIA_CHECK(mem != MAP_FAILED);

    if (mem == MAP_FAILED)
        return;

    reloc_arena a{};

    a.base     = static_cast<unsigned char*>(mem);
    a.capacity = cap;
    a.back     = cap;

    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::loader::image img;

    ASTIA_CHECK(
        astia::elf::loader::load(module_symbols_blob, module_symbols_blob_len, e, img));
    ASTIA_CHECK(img.init_count != 0);

    for (size_t i = 0; i < img.init_count; ++i) {
        if (img.init_array[i] != nullptr)
            invoke_module_initializer(img.init_array[i]);
    }

    if (img.entry != nullptr)
        ASTIA_CHECK(invoke_module_entry(img.entry) == astia::error_t::enone);

    for (size_t i = img.fini_count; i != 0; --i) {
        if (img.fini_array[i - 1] != nullptr)
            invoke_module_finalizer(img.fini_array[i - 1]);
    }

    ASTIA_CHECK(img.fini_count != 0);

    if (img.entry != nullptr)
        ASTIA_CHECK(invoke_module_entry(img.entry) == astia::error_t::enone);

    munmap(mem, cap);
}

ASTIA_TEST(elf_relocatable_rejects_bad_magic) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    buf.data[0] = 0x00;

    reloc_arena             a{};
    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::loader::image img;

    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
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

    a.base     = static_cast<unsigned char*>(mem);
    a.capacity = cap;

    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = nullptr;

    for (size_t i = 0; i < 32; ++i) {
        a.front   = 0;
        a.back    = cap;
        a.cache_n = 0;

        astia::elf::loader::image img;

        ASTIA_CHECK(
            !astia::elf::loader::load(hello_mod_blob, hello_mod_blob_len, e, img));
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

    reloc_arena             a{};
    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::loader::image img;

    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_non_relocatable) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    buf.data[16] = 2; // e_type (u16 LE); set ET_EXEC instead of ET_REL
    buf.data[17] = 0;

    reloc_arena             a{};
    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = reloc_resolve;

    astia::elf::loader::image img;

    // Now it identifies as an executable and takes the executable path, which
    // has no PT_LOAD / module_main to satisfy.
    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_unresolved_symbol) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    reloc_arena             a{};
    astia::elf::loader::env e{};

    e.ctx     = &a;
    e.map     = reloc_map;
    e.unmap   = reloc_unmap;
    e.resolve = nullptr; // no resolver: kernel_add cannot bind

    astia::elf::loader::image img;

    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_relocatable_rejects_unterminated_defined_symbol_name) {
    blob_copy  buf(hello_mod_blob, hello_mod_blob_len);
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

        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<unsigned long long>(buf.data[offset + i]) << (i * 8);
        }

        return value;
    };

    const unsigned long long section_offset = read_u64(40);
    const unsigned short     section_count  = read_u16(60);
    const unsigned short     section_size   = read_u16(58);
    size_t                   string_offset  = 0;
    size_t                   string_size    = 0;

    for (unsigned short i = 0; i < section_count; ++i) {
        const size_t section =
            static_cast<size_t>(section_offset) + static_cast<size_t>(i) * section_size;

        if (read_u32(section + 4) != 2)
            continue;

        const unsigned int string_index   = read_u32(section + 40);
        const size_t       string_section = static_cast<size_t>(section_offset) +
                                      static_cast<size_t>(string_index) * section_size;

        string_offset = static_cast<size_t>(read_u64(string_section + 24));
        string_size   = static_cast<size_t>(read_u64(string_section + 32));

        break;
    }

    ASTIA_CHECK(string_size != 0);

    if (string_size != 0) {
        size_t name_offset = string_size;

        for (size_t i = 0; i + sizeof("_Z11module_mainv") <= string_size; ++i) {
            if (std::memcmp(buf.data + string_offset + i, "_Z11module_mainv",
                            sizeof("_Z11module_mainv") - 1) == 0 &&
                buf.data[string_offset + i + sizeof("_Z11module_mainv") - 1] == '\0') {
                name_offset = i;

                break;
            }
        }

        ASTIA_CHECK(name_offset < string_size);

        if (name_offset < string_size)
            buf.data[string_offset + name_offset + sizeof("_Z11module_mainv") - 1] = 'x';

        reloc_arena             a{};
        astia::elf::loader::env e{};

        e.ctx     = &a;
        e.map     = reloc_map;
        e.unmap   = reloc_unmap;
        e.resolve = reloc_resolve;

        astia::elf::loader::image img;

        ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
    }
}

ASTIA_TEST(elf_relocatable_rejects_non_function_entry) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    const size_t symbol = module_symbol_offset(buf);

    ASTIA_CHECK(symbol != 0);

    if (symbol != 0)
        buf.data[symbol + 4] = 1;

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_hidden_entry) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    const size_t symbol = module_symbol_offset(buf);

    ASTIA_CHECK(symbol != 0);

    if (symbol != 0)
        buf.data[symbol + 5] = 1;

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_entry_outside_executable_section) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    const size_t symbol = module_symbol_offset(buf);

    ASTIA_CHECK(symbol != 0);

    if (symbol != 0) {
        const size_t section_offset = static_cast<size_t>(blob_u64(buf, 40));
        const size_t section_size =
            static_cast<size_t>(buf.data[58]) | (static_cast<size_t>(buf.data[59]) << 8);
        const size_t section_index = static_cast<size_t>(buf.data[symbol + 6]) |
                                     (static_cast<size_t>(buf.data[symbol + 7]) << 8);
        const size_t section = section_offset + section_index * section_size;

        blob_put_u64(buf, symbol + 8, blob_u64(buf, section + 32));
    }

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_duplicate_entry_definitions) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    size_t       entry_name   = 0;
    const size_t entry_symbol = module_symbol_offset(buf, &entry_name);

    ASTIA_CHECK(entry_symbol != 0);

    if (entry_symbol != 0) {
        const size_t section_offset = static_cast<size_t>(blob_u64(buf, 40));
        const size_t section_count =
            static_cast<size_t>(buf.data[60]) | (static_cast<size_t>(buf.data[61]) << 8);
        const size_t section_size =
            static_cast<size_t>(buf.data[58]) | (static_cast<size_t>(buf.data[59]) << 8);
        const size_t entry_section =
            static_cast<size_t>(buf.data[entry_symbol + 6]) |
            (static_cast<size_t>(buf.data[entry_symbol + 7]) << 8);
        bool copied = false;

        for (size_t i = 0; i < section_count && !copied; ++i) {
            const size_t       section = section_offset + i * section_size;
            const unsigned int type =
                static_cast<unsigned int>(buf.data[section + 4]) |
                (static_cast<unsigned int>(buf.data[section + 5]) << 8);
            if (type != 2)
                continue;

            const size_t symbols = static_cast<size_t>(blob_u64(buf, section + 32)) / 24;
            const size_t symbol_offset = static_cast<size_t>(blob_u64(buf, section + 24));
            const size_t string_index =
                static_cast<size_t>(buf.data[section + 40]) |
                (static_cast<size_t>(buf.data[section + 41]) << 8) |
                (static_cast<size_t>(buf.data[section + 42]) << 16) |
                (static_cast<size_t>(buf.data[section + 43]) << 24);
            const size_t string_section = section_offset + string_index * section_size;
            const size_t string_offset =
                static_cast<size_t>(blob_u64(buf, string_section + 24));

            for (size_t j = 0; j < symbols; ++j) {
                const size_t symbol = symbol_offset + j * 24;

                if (symbol == entry_symbol || j == 0 || (buf.data[symbol + 4] >> 4) != 1)
                    continue;

                blob_put_u32(buf, symbol,
                             static_cast<unsigned int>(entry_name - string_offset));
                blob_put_u16(buf, symbol + 6, static_cast<unsigned short>(entry_section));
                blob_put_u64(buf, symbol + 8, 0);
                blob_put_u64(buf, symbol + 16, 1);

                copied = true;

                break;
            }
        }

        ASTIA_CHECK(copied);
    }

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_synchronous_entry_abi) {
    blob_copy buf(module_sync_entry_blob, module_sync_entry_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_malformed_constructor_array) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t section_offset = static_cast<size_t>(blob_u64(buf, 40));
    const size_t section_count =
        static_cast<size_t>(buf.data[60]) | (static_cast<size_t>(buf.data[61]) << 8);
    const size_t section_size =
        static_cast<size_t>(buf.data[58]) | (static_cast<size_t>(buf.data[59]) << 8);
    bool found = false;

    for (size_t i = 0; i < section_count; ++i) {
        const size_t       section = section_offset + i * section_size;
        const unsigned int type    = static_cast<unsigned int>(buf.data[section + 4]) |
                                  (static_cast<unsigned int>(buf.data[section + 5]) << 8);
        if (type == 14) {
            blob_put_u64(buf, section + 56, 4);

            found = true;

            break;
        }
    }

    ASTIA_CHECK(found);
    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_malformed_priority_suffix) {
    blob_copy buf(module_priority_malformed_blob, module_priority_malformed_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_overflowing_priority_suffix) {
    blob_copy buf(module_priority_overflow_blob, module_priority_overflow_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_trailing_dot_init_priority_suffix) {
    blob_copy buf(module_priority_init_trailing_dot_blob,
                  module_priority_init_trailing_dot_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_trailing_dot_fini_priority_suffix) {
    blob_copy buf(module_priority_fini_trailing_dot_blob,
                  module_priority_fini_trailing_dot_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_out_of_bounds_section_string_table) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t shstr_index   = blob_u16(buf, 62);
    const size_t shstr_section = section_header_offset(buf, shstr_index);

    blob_put_u64(buf, shstr_section + 24, buf.len);
    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_out_of_bounds_section_name_offset) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t shstr_index   = blob_u16(buf, 62);
    const size_t shstr_section = section_header_offset(buf, shstr_index);
    const size_t shstr_size    = static_cast<size_t>(blob_u64(buf, shstr_section + 32));
    bool         found         = false;

    for (size_t i = 0; i < section_count(buf); ++i) {
        const size_t section = section_header_offset(buf, i);

        if (blob_u32(buf, section) == 0)
            continue;

        blob_put_u32(buf, section, static_cast<unsigned int>(shstr_size));

        found = true;

        break;
    }

    ASTIA_CHECK(found);
    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_section_name_without_nul) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t shstr_index   = blob_u16(buf, 62);
    const size_t shstr_section = section_header_offset(buf, shstr_index);
    const size_t shstr_offset  = static_cast<size_t>(blob_u64(buf, shstr_section + 24));
    const size_t shstr_size    = static_cast<size_t>(blob_u64(buf, shstr_section + 32));

    ASTIA_CHECK(shstr_size != 0);

    if (shstr_size == 0)
        return;

    for (size_t i = 0; i < section_count(buf); ++i) {
        const size_t section = section_header_offset(buf, i);

        if (blob_u32(buf, section) == 0)
            continue;

        blob_put_u32(buf, section, static_cast<unsigned int>(shstr_size - 1));

        buf.data[shstr_offset + shstr_size - 1] = 'x';

        ASTIA_CHECK(!load_relocatable_fixture(buf));

        return;
    }

    ASTIA_CHECK(false);
}

ASTIA_TEST(elf_relocatable_rejects_out_of_bounds_symbol_name_offset) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t symbol = module_symbol_offset(buf);

    ASTIA_CHECK(symbol != 0);

    if (symbol == 0)
        return;

    bool found = false;

    for (size_t i = 0; i < section_count(buf); ++i) {
        const size_t section = section_header_offset(buf, i);

        if (static_cast<unsigned int>(blob_u32(buf, section + 4)) != 2)
            continue;

        const size_t symbols = static_cast<size_t>(blob_u64(buf, section + 32)) / 24;
        const size_t symbol_offset = static_cast<size_t>(blob_u64(buf, section + 24));

        for (size_t j = 0; j < symbols; ++j) {
            if (symbol_offset + j * 24 == symbol) {
                const size_t string_index   = blob_u32(buf, section + 40);
                const size_t string_section = section_header_offset(buf, string_index);
                const size_t string_size =
                    static_cast<size_t>(blob_u64(buf, string_section + 32));

                blob_put_u32(buf, symbol, static_cast<unsigned int>(string_size));

                found = true;

                break;
            }
        }

        if (found)
            break;
    }

    ASTIA_CHECK(found);
    ASTIA_CHECK(!load_relocatable_fixture(buf));
}

ASTIA_TEST(elf_relocatable_rejects_symbol_name_without_nul) {
    blob_copy buf(module_symbols_blob, module_symbols_blob_len);

    const size_t symbol = module_symbol_offset(buf);

    ASTIA_CHECK(symbol != 0);

    if (symbol == 0)
        return;

    for (size_t i = 0; i < section_count(buf); ++i) {
        const size_t section = section_header_offset(buf, i);

        if (static_cast<unsigned int>(blob_u32(buf, section + 4)) != 2)
            continue;

        const size_t symbols = static_cast<size_t>(blob_u64(buf, section + 32)) / 24;
        const size_t symbol_offset = static_cast<size_t>(blob_u64(buf, section + 24));

        if (symbol < symbol_offset || symbol >= symbol_offset + symbols * 24)
            continue;

        const size_t string_index   = blob_u32(buf, section + 40);
        const size_t string_section = section_header_offset(buf, string_index);
        const size_t string_offset =
            static_cast<size_t>(blob_u64(buf, string_section + 24));
        const size_t string_size =
            static_cast<size_t>(blob_u64(buf, string_section + 32));

        ASTIA_CHECK(string_size != 0);

        if (string_size == 0)
            return;

        blob_put_u32(buf, symbol, static_cast<unsigned int>(string_size - 1));

        buf.data[string_offset + string_size - 1] = 'x';

        ASTIA_CHECK(!load_relocatable_fixture(buf));

        return;
    }

    ASTIA_CHECK(false);
}

ASTIA_TEST(elf_relocatable_rejects_bad_alignment_and_table_sizes) {
    auto load_bad = [](blob_copy& buf) {
        unsigned char storage[1 << 16]{};

        reloc_arena arena{};

        arena.base     = storage;
        arena.capacity = sizeof(storage);
        arena.back     = sizeof(storage);

        astia::elf::loader::env environment{};

        environment.ctx     = &arena;
        environment.map     = reloc_map;
        environment.unmap   = reloc_unmap;
        environment.resolve = reloc_resolve;

        astia::elf::loader::image image;

        const bool loaded =
            astia::elf::loader::load(buf.data, buf.len, environment, image);
        if (loaded)
            reloc_unmap(&arena, image.base, image.size);

        return loaded;
    };

    blob_copy zero_alignment(hello_mod_blob, hello_mod_blob_len);

    const size_t section_offset = static_cast<size_t>(blob_u64(zero_alignment, 40));
    const size_t section_size   = static_cast<size_t>(zero_alignment.data[58]) |
                                (static_cast<size_t>(zero_alignment.data[59]) << 8);

    blob_put_u64(zero_alignment, section_offset + section_size * 4 + 48, 0);
    ASTIA_CHECK(load_bad(zero_alignment));

    blob_copy bad_alignment(hello_mod_blob, hello_mod_blob_len);

    blob_put_u64(bad_alignment, section_offset + section_size + 48, 3);
    ASTIA_CHECK(!load_bad(bad_alignment));

    blob_copy bad_symbol_size(hello_mod_blob, hello_mod_blob_len);

    const size_t section_count = static_cast<size_t>(bad_symbol_size.data[60]) |
                                 (static_cast<size_t>(bad_symbol_size.data[61]) << 8);

    for (size_t i = 0; i < section_count; ++i) {
        const size_t       section = section_offset + i * section_size;
        const unsigned int type =
            static_cast<unsigned int>(bad_symbol_size.data[section + 4]) |
            (static_cast<unsigned int>(bad_symbol_size.data[section + 5]) << 8) |
            (static_cast<unsigned int>(bad_symbol_size.data[section + 6]) << 16) |
            (static_cast<unsigned int>(bad_symbol_size.data[section + 7]) << 24);
        if (type != 2)
            continue;

        const auto size = blob_u64(bad_symbol_size, section + 32);

        blob_put_u64(bad_symbol_size, section + 32, size - 1);
        break;
    }

    ASTIA_CHECK(!load_bad(bad_symbol_size));

    blob_copy bad_rela_size(hello_mod_blob, hello_mod_blob_len);

    for (size_t i = 0; i < section_count; ++i) {
        const size_t       section = section_offset + i * section_size;
        const unsigned int type =
            static_cast<unsigned int>(bad_rela_size.data[section + 4]) |
            (static_cast<unsigned int>(bad_rela_size.data[section + 5]) << 8) |
            (static_cast<unsigned int>(bad_rela_size.data[section + 6]) << 16) |
            (static_cast<unsigned int>(bad_rela_size.data[section + 7]) << 24);
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
    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    ASTIA_CHECK(astia::elf::loader::load(fixture_dyn_blob, fixture_dyn_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::loader::kind::dyn);
    ASTIA_CHECK(img.load_bias != 0); // a PIE is placed away from its link address
    ASTIA_CHECK(img.nsegs >= 1);
    ASTIA_CHECK(img.entry != nullptr);

    // The startup vector and dlmap both expose the phdr table through the
    // image's runtime address; check the loader derives it from the first
    // PT_LOAD rather than handing out the file offset.
    {
        const auto* bytes  = static_cast<const unsigned char*>(fixture_dyn_blob);
        auto        read16 = [&](astia::usize off) {
            return static_cast<astia::u16>(bytes[off] | (bytes[off + 1] << 8));
        };
        auto read64 = [&](astia::usize off) {
            astia::u64 value = 0;

            for (astia::usize byte = 0; byte < 8; ++byte) {
                value |= static_cast<astia::u64>(bytes[off + byte]) << (byte * 8);
            }

            return value;
        };

        ASTIA_CHECK(img.phent == read16(54));
        ASTIA_CHECK(img.phnum == read16(56));
        ASTIA_CHECK(img.phdr_vaddr == img.load_bias + read64(32));
    }

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
    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    ASTIA_CHECK(
        astia::elf::loader::load(fixture_exec_blob, fixture_exec_blob_len, e, img));
    ASTIA_CHECK(img.loaded == astia::elf::loader::kind::executable);
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

ASTIA_TEST(elf_accepts_nonpage_load_alignment) {
    blob_copy               dyn_blob = make_nonpage_aligned_executable(false, true);
    exec_arena              dyn_arena{};
    astia::elf::loader::env dyn_env{};

    dyn_env.ctx   = &dyn_arena;
    dyn_env.map   = congruent_exec_map;
    dyn_env.unmap = exec_unmap;

    astia::elf::loader::image dyn_image{};

    ASTIA_CHECK(
        astia::elf::loader::load(dyn_blob.data, dyn_blob.len, dyn_env, dyn_image));
    ASTIA_CHECK(dyn_arena.mapped == 1);

    if (dyn_arena.base != nullptr)
        munmap(dyn_arena.base, dyn_arena.span);

    blob_copy               fixed_blob = make_nonpage_aligned_executable(true, true);
    exec_arena              fixed_arena{};
    astia::elf::loader::env fixed_env{};

    fixed_env.ctx   = &fixed_arena;
    fixed_env.map   = congruent_exec_map;
    fixed_env.unmap = exec_unmap;

    astia::elf::loader::image fixed_image{};

    ASTIA_CHECK(astia::elf::loader::load(fixed_blob.data, fixed_blob.len, fixed_env,
                                         fixed_image));
    ASTIA_CHECK(fixed_arena.mapped == 1);

    if (fixed_arena.base != nullptr)
        munmap(fixed_arena.base, fixed_arena.span);

    stream_exec_context           stream_context{dyn_blob.data, dyn_blob.len, {}};
    astia::elf::loader::async_env stream_env{};

    stream_env.ctx   = &stream_context;
    stream_env.map   = congruent_stream_map;
    stream_env.read  = stream_read;
    stream_env.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(stream_env, dyn_blob.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(stream_context.arena.mapped == 1);

    if (stream_context.arena.base != nullptr)
        munmap(stream_context.arena.base, stream_context.arena.span);
}

ASTIA_TEST(elf_rejects_noncongruent_load_alignment) {
    blob_copy               blob = make_nonpage_aligned_executable(false, false);
    exec_arena              arena{};
    astia::elf::loader::env environment{};

    environment.ctx   = &arena;
    environment.map   = congruent_exec_map;
    environment.unmap = exec_unmap;

    astia::elf::loader::image image{};

    ASTIA_CHECK(!astia::elf::loader::load(blob.data, blob.len, environment, image));
    ASTIA_CHECK(arena.mapped == 0);
}

ASTIA_TEST(elf_nonstream_unaligned_relative_relocation_is_safe) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);

    shift_first_relocation_target(buf, -1);

    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    ASTIA_CHECK(astia::elf::loader::load(buf.data, buf.len, e, img));
    ASTIA_CHECK(a.mapped == 1);
    ASTIA_CHECK(a.unmapped == 0);

    if (a.base != nullptr)
        munmap(a.base, a.span);
}

ASTIA_TEST(elf_executable_failures_release_mapped_span) {
    blob_copy relocation_failure(fixture_dyn_blob, fixture_dyn_blob_len);

    reject_first_dynamic_relocation(relocation_failure);

    exec_arena              relocation_arena{};
    astia::elf::loader::env relocation_env{};

    relocation_env.ctx   = &relocation_arena;
    relocation_env.map   = exec_map;
    relocation_env.unmap = exec_unmap;

    astia::elf::loader::image relocation_image;

    ASTIA_CHECK(!astia::elf::loader::load(relocation_failure.data, relocation_failure.len,
                                          relocation_env, relocation_image));
    ASTIA_CHECK(relocation_arena.mapped == 1);
    ASTIA_CHECK(relocation_arena.unmapped == 1);

    exec_arena              copy_arena{};
    astia::elf::loader::env copy_env{};

    copy_env.ctx   = &copy_arena;
    copy_env.map   = exec_map;
    copy_env.unmap = exec_unmap;
    copy_env.copy  = copy_failure;

    astia::elf::loader::image copy_image;

    ASTIA_CHECK(!astia::elf::loader::load(fixture_dyn_blob, fixture_dyn_blob_len,
                                          copy_env, copy_image));
    ASTIA_CHECK(copy_arena.mapped == 1);
    ASTIA_CHECK(copy_arena.unmapped == 1);

    exec_arena              fixed_arena{};
    astia::elf::loader::env fixed_env{};

    fixed_env.ctx   = &fixed_arena;
    fixed_env.map   = misfixed_map;
    fixed_env.unmap = exec_unmap;

    astia::elf::loader::image fixed_image;

    ASTIA_CHECK(!astia::elf::loader::load(fixture_exec_blob, fixture_exec_blob_len,
                                          fixed_env, fixed_image));
    ASTIA_CHECK(fixed_arena.mapped == 1);
    ASTIA_CHECK(fixed_arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_unaligned_relative_relocation_is_safe) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);

    shift_first_relocation_target(buf, -1);

    stream_exec_context           context{buf.data, buf.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, buf.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(task.value() != nullptr);
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 0);

    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_direct_read_applies_relative_relocations) {
    stream_exec_context           context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx     = &context;
    environment.map     = stream_map;
    environment.read    = stream_read;
    environment.read_to = stream_read_to;
    environment.copy    = stream_copy;
    environment.unmap   = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(context.direct_reads > 0);
    ASTIA_CHECK(context.copy_calls > 0);

    if (!task.failed() && task.value() != nullptr) {
        auto entry = reinterpret_cast<int (*)()>(task.value()->entry);

        ASTIA_CHECK(entry() == 42);
        ASTIA_CHECK(entry() == 44);
    }

    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_fallback_read_copies_when_direct_reader_is_absent) {
    blob_copy                     blob = make_stream_boundary_executable();
    stream_exec_context           context{blob.data, blob.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.copy  = stream_copy;
    environment.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, blob.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(context.direct_reads == 0);
    ASTIA_CHECK(context.copy_calls == 6);

    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_uses_bounded_reads) {
    stream_exec_context           context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(context.read_calls > 0);
    ASTIA_CHECK(context.max_read <= 4096);
    ASTIA_CHECK(context.read_bytes < fixture_dyn_blob_len);

    if (!task.failed() && task.value() != nullptr) {
        const auto& image = *task.value();

        const size_t phoff = static_cast<size_t>(
            blob_u64(blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), 32));
        const size_t phnum = static_cast<size_t>(fixture_dyn_blob[56]) |
                             (static_cast<size_t>(fixture_dyn_blob[57]) << 8);
        const size_t phentsize = static_cast<size_t>(fixture_dyn_blob[54]) |
                                 (static_cast<size_t>(fixture_dyn_blob[55]) << 8);

        for (size_t i = 0; i < phnum; ++i) {
            const size_t       ph = phoff + i * phentsize;
            const unsigned int type =
                static_cast<unsigned int>(fixture_dyn_blob[ph]) |
                (static_cast<unsigned int>(fixture_dyn_blob[ph + 1]) << 8) |
                (static_cast<unsigned int>(fixture_dyn_blob[ph + 2]) << 16) |
                (static_cast<unsigned int>(fixture_dyn_blob[ph + 3]) << 24);
            if (type != 1)
                continue;

            const auto vaddr =
                blob_u64(blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 16);
            const auto offset =
                blob_u64(blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 8);
            const auto filesz =
                blob_u64(blob_copy(fixture_dyn_blob, fixture_dyn_blob_len), ph + 32);

            bool found = false;

            for (size_t seg = 0; seg < image.nsegs; ++seg) {
                if (image.segs[seg].vaddr != vaddr)
                    continue;

                found = true;

                ASTIA_CHECK(image.segs[seg].filesz == filesz);

                const size_t sample = filesz < 16 ? static_cast<size_t>(filesz) : 16;

                ASTIA_CHECK(std::memcmp(image.segs[seg].loc, fixture_dyn_blob + offset,
                                        sample) == 0);
            }

            ASTIA_CHECK(found);
        }
    }

    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_reads_segments_directly_into_destination) {
    blob_copy                     blob = make_stream_boundary_executable();
    stream_exec_context           context{blob.data, blob.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx     = &context;
    environment.map     = stream_map;
    environment.read    = stream_read;
    environment.read_to = stream_read_to;
    environment.copy    = stream_copy;
    environment.unmap   = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, blob.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(!task.failed());
    ASTIA_CHECK(context.direct_reads == 3);
    ASTIA_CHECK(context.copy_calls == 3);
    ASTIA_CHECK(task.value() != nullptr);

    if (!task.failed() && task.value() != nullptr) {
        const auto& image = *task.value();

        ASTIA_CHECK(image.nsegs == 1);

        auto* loaded = static_cast<const unsigned char*>(image.segs[0].loc);

        for (size_t i : {size_t{0}, size_t{4095}, size_t{4096}, size_t{8191},
                         size_t{8192}, size_t{0x20ff}}) {
            ASTIA_CHECK(loaded[i] == static_cast<unsigned char>(i * 37u + 11u));
        }

        for (size_t i = 0x2100; i < 0x3000; ++i) {
            ASTIA_CHECK(loaded[i] == 0);
        }
    }

    if (context.arena.base != nullptr)
        munmap(context.arena.base, context.arena.span);
}

ASTIA_TEST(elf_stream_rejects_truncated_program_headers) {
    const size_t phoff = static_cast<size_t>(
        blob_u64(blob_copy(fixture_exec_blob, fixture_exec_blob_len), 32));
    const size_t phnum = static_cast<size_t>(fixture_exec_blob[56]) |
                         (static_cast<size_t>(fixture_exec_blob[57]) << 8);
    const size_t truncated_size = phoff + phnum * 56 - 1;

    stream_exec_context           context{fixture_exec_blob, truncated_size, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, context.source_size);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 0);
}

ASTIA_TEST(elf_stream_rejects_malformed_segment_sizes) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    const size_t phoff = static_cast<size_t>(blob_u64(buf, 32));
    const size_t phentsize =
        static_cast<size_t>(buf.data[54]) | (static_cast<size_t>(buf.data[55]) << 8);
    const size_t phnum =
        static_cast<size_t>(buf.data[56]) | (static_cast<size_t>(buf.data[57]) << 8);
    bool changed = false;

    for (size_t i = 0; i < phnum; ++i) {
        const size_t       ph   = phoff + i * phentsize;
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

    stream_exec_context           context{buf.data, buf.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, buf.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 0);
}

ASTIA_TEST(elf_stream_read_failure_releases_mapping) {
    stream_exec_context context{fixture_dyn_blob, fixture_dyn_blob_len, {}};

    context.fail_read_after = 3;

    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_direct_read_failure_releases_mapping) {
    blob_copy           blob = make_stream_boundary_executable();
    stream_exec_context context{blob.data, blob.len, {}};

    context.fail_read_after = 3;

    astia::elf::loader::async_env environment{};

    environment.ctx     = &context;
    environment.map     = stream_map;
    environment.read    = stream_read;
    environment.read_to = stream_read_to;
    environment.copy    = stream_copy;
    environment.unmap   = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, blob.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.direct_reads > 0);
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_rejects_scratch_allocation_failure) {
    stream_exec_context           context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    astia_test::fail_next_allocation();
    task.resume();
    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 0);
}

ASTIA_TEST(elf_stream_destroy_releases_mapping) {
    stream_exec_context           context{fixture_dyn_blob, fixture_dyn_blob_len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = pausing_stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    while (!task.done() && !context.paused) {
        task.resume();
    }

    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.paused);
    task.reset();
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_direct_read_destroy_releases_mapping) {
    blob_copy           blob = make_stream_boundary_executable();
    stream_exec_context context{blob.data, blob.len, {}};

    context.pause_next_direct_read = true;

    astia::elf::loader::async_env environment{};

    environment.ctx     = &context;
    environment.map     = stream_map;
    environment.read    = stream_read;
    environment.read_to = stream_read_to;
    environment.copy    = stream_copy;
    environment.unmap   = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, blob.len);

    while (!task.done() && !context.direct_paused) {
        task.resume();
    }

    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.direct_paused);
    task.reset();
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_copy_failure_releases_mapping) {
    stream_exec_context context{fixture_dyn_blob, fixture_dyn_blob_len, {}};

    context.fail_copy_after = 1;

    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.copy  = stream_copy;
    environment.unmap = stream_unmap;

    auto task =
        astia::elf::loader::load_executable_stream(environment, fixture_dyn_blob_len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_relocation_failure_releases_mapping) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);

    reject_first_dynamic_relocation(buf);

    stream_exec_context           context{buf.data, buf.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, buf.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_stream_tls_failure_releases_mapping) {
    blob_copy buf(fixture_dyn_blob, fixture_dyn_blob_len);

    const size_t phoff = static_cast<size_t>(blob_u64(buf, 32));
    const size_t phentsize =
        static_cast<size_t>(buf.data[54]) | (static_cast<size_t>(buf.data[55]) << 8);
    const size_t phnum =
        static_cast<size_t>(buf.data[56]) | (static_cast<size_t>(buf.data[57]) << 8);
    bool changed = false;

    for (size_t i = 0; i < phnum; ++i) {
        const size_t       ph   = phoff + i * phentsize;
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

    stream_exec_context           context{buf.data, buf.len, {}};
    astia::elf::loader::async_env environment{};

    environment.ctx   = &context;
    environment.map   = stream_map;
    environment.read  = stream_read;
    environment.unmap = stream_unmap;

    auto task = astia::elf::loader::load_executable_stream(environment, buf.len);

    while (!task.done()) {
        task.resume();
    }

    ASTIA_CHECK(task.failed());
    ASTIA_CHECK(context.arena.mapped == 1);
    ASTIA_CHECK(context.arena.unmapped == 1);
}

ASTIA_TEST(elf_executable_rejects_bad_magic) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    buf.data[0] = 0x00;

    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_executable_rejects_wrong_machine) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    buf.data[18] = 0x03; // e_machine -> EM_386, not x86-64
    buf.data[19] = 0x00;

    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    ASTIA_CHECK(!astia::elf::loader::load(buf.data, buf.len, e, img));
}

ASTIA_TEST(elf_executable_rejects_truncated_phdrs) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    exec_arena              a{};
    astia::elf::loader::env e{};

    e.ctx   = &a;
    e.map   = exec_map;
    e.unmap = exec_unmap;

    astia::elf::loader::image img;

    // Shorter than the program-header table the header points at.
    ASTIA_CHECK(!astia::elf::loader::load(buf.data, 60, e, img));
}

ASTIA_TEST(elf_audit_accepts_relocatable_module) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    ASTIA_CHECK(astia::elf::loader::audit_relocatable(astia::span<const astia::u8>(
                    buf.data, buf.len)) == astia::elf::loader::relocatable_problem::none);
    ASTIA_CHECK(std::strcmp(astia::elf::loader::relocatable_problem_text(
                                astia::elf::loader::relocatable_problem::none),
                            "compatible") == 0);
}

ASTIA_TEST(elf_audit_rejects_32bit_absolute_relocation) {
    blob_copy buf(module_lifecycle_reloc32_blob, module_lifecycle_reloc32_blob_len);

    ASTIA_CHECK(astia::elf::loader::audit_relocatable(
                    astia::span<const astia::u8>(buf.data, buf.len)) ==
                astia::elf::loader::relocatable_problem::incompatible_code_model);
    ASTIA_CHECK(
        std::strstr(astia::elf::loader::relocatable_problem_text(
                        astia::elf::loader::relocatable_problem::incompatible_code_model),
                    "-mcmodel=large") != nullptr);
}

ASTIA_TEST(elf_audit_rejects_unsupported_relocation) {
    blob_copy buf(hello_mod_blob, hello_mod_blob_len);

    const size_t info = first_allocated_relocation_info_offset(buf);

    ASTIA_CHECK(info != 0);

    if (info == 0)
        return;

    blob_put_u32(buf, info, 9); // R_X86_64_GOTPCREL is outside the module ABI

    ASTIA_CHECK(astia::elf::loader::audit_relocatable(
                    astia::span<const astia::u8>(buf.data, buf.len)) ==
                astia::elf::loader::relocatable_problem::unsupported_relocation);
}

ASTIA_TEST(elf_audit_rejects_non_relocatable_image) {
    blob_copy buf(fixture_exec_blob, fixture_exec_blob_len);

    ASTIA_CHECK(astia::elf::loader::audit_relocatable(
                    astia::span<const astia::u8>(buf.data, buf.len)) ==
                astia::elf::loader::relocatable_problem::not_relocatable);
}

ASTIA_TEST(elf_relocatable_loader_rejects_32bit_absolute_relocation_directly) {
    blob_copy buf(module_lifecycle_reloc32_blob, module_lifecycle_reloc32_blob_len);

    ASTIA_CHECK(!load_relocatable_fixture(buf));
}
