#include <astia/boot/virt/pvh.hpp>

#include "test.hpp"

// Mirrors the on-the-wire PVH layout; the real structs are private to pvh.cpp.
namespace {
    struct raw_start_info {
        astia::u32 magic;
        astia::u32 version;
        astia::u32 flags;
        astia::u32 nr_modules;
        astia::u64 modlist_paddr;
        astia::u64 cmdline_paddr;
        astia::u64 rsdp_paddr;
        astia::u64 memmap_paddr;
        astia::u32 memmap_entries;
        astia::u32 reserved;
    };

    struct raw_memmap_entry {
        astia::u64 addr;
        astia::u64 size;
        astia::u32 type;
        astia::u32 reserved;
    };

    struct raw_modlist_entry {
        astia::u64 paddr;
        astia::u64 size;
        astia::u64 cmdline_paddr;
        astia::u64 reserved;
    };
} // namespace

ASTIA_TEST(pvh_parses_memmap) {
    const raw_memmap_entry entries[2] = {
        {0x100000, 0x400000, 1, 0}, // usable
        {0, 0xa0000, 2, 0},         // reserved
    };

    raw_start_info info = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.memmap_paddr   = reinterpret_cast<astia::u64>(entries);
    info.memmap_entries = 2;

    astia::boot::boot_info out{};

    ASTIA_CHECK(astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));
    ASTIA_CHECK(out.memory.count() == 2);
    ASTIA_CHECK(out.memory[0].base == 0x100000);
    ASTIA_CHECK(out.memory[0].size == 0x400000);
    ASTIA_CHECK(out.memory[0].type == astia::mm::memory_type::usable);
    ASTIA_CHECK(out.memory[1].base == 0);
    ASTIA_CHECK(out.memory[1].type == astia::mm::memory_type::reserved);
}

ASTIA_TEST(pvh_parses_handoff_fields) {
    const raw_memmap_entry entries[1] = {
        {0x100000, 0x400000, 1, 0},
    };

    const char cmdline[] = "quiet splash";

    const raw_modlist_entry modlist[2] = {
        {0x4000000, 0x1000, 0, 0}, // the ramdisk: the first module
        {0x5000000, 0x2000, 0, 0}, // a module after it
    };

    raw_start_info info = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.modlist_paddr  = reinterpret_cast<astia::u64>(modlist);
    info.nr_modules     = 2;
    info.cmdline_paddr  = reinterpret_cast<astia::u64>(cmdline);
    info.rsdp_paddr     = 0xfee00000;
    info.memmap_paddr   = reinterpret_cast<astia::u64>(entries);
    info.memmap_entries = 1;

    astia::boot::boot_info out{};

    ASTIA_CHECK(astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));

    ASTIA_CHECK(out.cmdline != nullptr);
    ASTIA_CHECK(out.cmdline[0] == 'q');
    ASTIA_CHECK(out.cmdline[5] == ' ');

    ASTIA_CHECK(out.rsdp == 0xfee00000);

    ASTIA_CHECK(out.initrd.paddr == 0x4000000);
    ASTIA_CHECK(out.initrd.size == 0x1000);

    ASTIA_CHECK(out.module_count == 1);
    ASTIA_CHECK(out.modules[0].paddr == 0x5000000);
    ASTIA_CHECK(out.modules[0].size == 0x2000);
}

// The QEMU direct-boot case: -initrd alone lands in the modlist as its only
// entry, so the ramdisk is set and there is nothing left to call a module.
ASTIA_TEST(pvh_ramdisk_alone_leaves_no_modules) {
    const raw_memmap_entry entries[1] = {
        {0x100000, 0x400000, 1, 0},
    };
    const raw_modlist_entry modlist[1] = {
        {0x4000000, 0x1000, 0, 0},
    };

    raw_start_info info = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.modlist_paddr  = reinterpret_cast<astia::u64>(modlist);
    info.nr_modules     = 1;
    info.memmap_paddr   = reinterpret_cast<astia::u64>(entries);
    info.memmap_entries = 1;

    astia::boot::boot_info out{};

    ASTIA_CHECK(astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));
    ASTIA_CHECK(out.initrd.paddr == 0x4000000);
    ASTIA_CHECK(out.initrd.size == 0x1000);
    ASTIA_CHECK(out.module_count == 0);
}

ASTIA_TEST(pvh_absent_fields_stay_null) {
    const raw_memmap_entry entries[1] = {
        {0x100000, 0x400000, 1, 0},
    };

    raw_start_info info = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.memmap_paddr   = reinterpret_cast<astia::u64>(entries);
    info.memmap_entries = 1;

    astia::boot::boot_info out{};

    ASTIA_CHECK(astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));
    ASTIA_CHECK(out.cmdline == nullptr);
    ASTIA_CHECK(out.rsdp == 0);
    ASTIA_CHECK(out.initrd.paddr == 0);
    ASTIA_CHECK(out.module_count == 0);
}

ASTIA_TEST(pvh_rejects_bad_magic) {
    raw_start_info info = {};

    info.version        = 1;
    info.memmap_entries = 1;

    astia::boot::boot_info out{};

    ASTIA_CHECK(!astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));
}

ASTIA_TEST(pvh_rejects_missing_memmap) {
    raw_start_info info = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.memmap_entries = 0;

    astia::boot::boot_info out{};

    ASTIA_CHECK(!astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>::bytes(0, static_cast<astia::usize>(-1)), out));
}

ASTIA_TEST(pvh_rejects_tables_outside_trusted_memory) {
    const raw_memmap_entry entries[1] = {{0x100000, 0x400000, 1, 0}};
    raw_start_info         info       = {};

    info.magic          = 0x336EC578u;
    info.version        = 1;
    info.memmap_paddr   = reinterpret_cast<astia::u64>(entries);
    info.memmap_entries = 1;
    info.nr_modules     = 1;
    info.modlist_paddr  = 0x1234;

    astia::boot::boot_info out{};

    ASTIA_CHECK(!astia::boot::virt::parse_start_info(
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(&info),
                                     sizeof(info)),
        astia::span<const astia::u8>(reinterpret_cast<const astia::u8*>(entries),
                                     sizeof(entries)),
        out));
}

ASTIA_TEST(pvh_rejects_null) {
    astia::boot::boot_info out{};

    ASTIA_CHECK(
        !astia::boot::virt::parse_start_info(astia::span<const astia::u8>(), out));
}
