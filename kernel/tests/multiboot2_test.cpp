#include "multiboot2.hpp"
#include "test.hpp"

namespace {
    struct tag {
        astia::u32 type;
        astia::u32 size;
    };
    struct mmap {
        tag        h;
        astia::u32 entry_size;
        astia::u32 version;
    };
    struct entry {
        astia::u64 base, length;
        astia::u32 type, reserved;
    };
}

ASTIA_TEST(multiboot2_parses_requested_boot_data) {
    alignas(8) astia::u8 raw[104]{};
    *reinterpret_cast<astia::u32*>(raw) = sizeof(raw);
    astia::usize o                      = 8;
    const char   cmd[]                  = "root=/dev/ram0";
    auto*        c                      = reinterpret_cast<tag*>(raw + o);
    c->type                             = 1;
    c->size                             = 8 + sizeof(cmd);
    __builtin_memcpy(raw + o + 8, cmd, sizeof(cmd));
    o += 24;
    auto* m       = reinterpret_cast<mmap*>(raw + o);
    m->h          = {6, 40};
    m->entry_size = 24;
    auto* e       = reinterpret_cast<entry*>(raw + o + 16);
    *e            = {0x100000, 0x400000, 1, 0};
    o += 40;
    auto* mod                                    = reinterpret_cast<tag*>(raw + o);
    mod->type                                    = 3;
    mod->size                                    = 24;
    *reinterpret_cast<astia::u32*>(raw + o + 8)  = 0x4000000;
    *reinterpret_cast<astia::u32*>(raw + o + 12) = 0x4010000;
    raw[o + 16]                                  = 'i';
    raw[o + 17]                                  = 'n';
    raw[o + 18]                                  = 'i';
    raw[o + 19]                                  = 't';
    o += 24;
    auto* end = reinterpret_cast<tag*>(raw + o);
    end->type = 0;
    end->size = 8;

    astia::boot::boot_info out{};
    ASTIA_CHECK(astia::boot::virt::parse_multiboot2_info(raw, out));
    ASTIA_CHECK(out.memory.count() == 1);
    ASTIA_CHECK(out.memory[0].base == 0x100000);
    ASTIA_CHECK(out.cmdline != nullptr && out.cmdline[5] == '/');
    ASTIA_CHECK(out.initrd.paddr == 0x4000000 && out.initrd.size == 0x10000);
}

ASTIA_TEST(multiboot2_rejects_bad_bounds) {
    alignas(8) astia::u8 raw[24]{};
    *reinterpret_cast<astia::u32*>(raw) = sizeof(raw);
    auto* bad                           = reinterpret_cast<tag*>(raw + 8);
    bad->type                           = 6;
    bad->size                           = 0xffff;
    astia::boot::boot_info out{};
    ASTIA_CHECK(!astia::boot::virt::parse_multiboot2_info(raw, out));
}
