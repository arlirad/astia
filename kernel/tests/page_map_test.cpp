#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "arena.hpp"
#include "test.hpp"

namespace {

    using astia::arch::page_flags;
    using astia::arch::page_map;

    constexpr page_flags rw = page_flags::present | page_flags::writable;

    // page_map pulls its tables from the global kernel allocator, so point
    // that at the arena once and then hand out fresh address spaces.
    auto make_map(const page_map* parent = nullptr) -> page_map {
        astia_test::map_arena();

        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;
            map.add(astia_test::arena_base, astia_test::arena_size, astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);
            ready = true;
        }

        return page_map::create(parent);
    }

    // An address inside the kernel half (kernel_base's PML4 slot).
    auto kvirt(astia::u64 offset) -> astia::u64 { return astia::arch::kernel_base + offset; }

} // namespace

ASTIA_TEST(page_map_create_allocates_empty_root) {
    auto pm = make_map();
    ASTIA_CHECK(pm.valid());
    ASTIA_CHECK((pm.root() % 4096) == 0);
    ASTIA_CHECK(pm.parent() == nullptr);
    // A fresh root maps nothing.
    ASTIA_CHECK(pm.translate(0x1000) == page_map::invalid);
}

ASTIA_TEST(page_map_map_4k_round_trip) {
    auto pm = make_map();
    const astia::u64 phys = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(pm.map_4k(0x400000, phys, rw));
    ASTIA_CHECK(pm.translate(0x400000) == phys);
    ASTIA_CHECK(pm.translate(0x400fff) == phys + 0xfff);
    // The next page is a different slot and stays unmapped.
    ASTIA_CHECK(pm.translate(0x401000) == page_map::invalid);
}

ASTIA_TEST(page_map_map_2m_round_trip) {
    auto pm = make_map();
    const astia::u64 phys = astia_test::arena_base + 0x400000; // 2 MiB aligned

    ASTIA_CHECK(pm.map_2m(0x200000, phys, rw));
    ASTIA_CHECK(pm.translate(0x200000) == phys);
    ASTIA_CHECK(pm.translate(0x2fffff) == phys + 0xfffff);
}

ASTIA_TEST(page_map_map_4k_splits_2m_page) {
    auto pm = make_map();
    const astia::u64 base_phys = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(pm.map_2m(0x200000, base_phys, rw));

    // Punch a single 4 KiB page through the middle of the 2 MiB leaf; the
    // split must keep the surrounding frames mapped one page at a time.
    const astia::u64 page_phys = astia_test::arena_base + 0x600000;
    ASTIA_CHECK(pm.map_4k(0x201000, page_phys, rw));

    ASTIA_CHECK(pm.translate(0x201000) == page_phys);
    ASTIA_CHECK(pm.translate(0x200000) == base_phys);
    ASTIA_CHECK(pm.translate(0x203000) == base_phys + 0x3000);
    ASTIA_CHECK(pm.translate(0x2fffff) == base_phys + 0xfffff);
}

ASTIA_TEST(page_map_unmap_removes_4k) {
    auto pm = make_map();

    ASTIA_CHECK(pm.map_4k(0x500000, astia_test::arena_base + 0x300000, rw));
    ASTIA_CHECK(pm.unmap(0x500000));
    ASTIA_CHECK(pm.translate(0x500000) == page_map::invalid);
    // Unmapping something that is not there fails.
    ASTIA_CHECK(!pm.unmap(0x500000));
    ASTIA_CHECK(!pm.unmap(0x999000));
}

ASTIA_TEST(page_map_map_2m_refuses_existing_table) {
    auto pm = make_map();

    ASTIA_CHECK(pm.map_4k(0x300000, astia_test::arena_base + 0x300000, rw));
    // 0x200000..0x400000 already has a page table, so a 2 MiB leaf cannot
    // replace it.
    ASTIA_CHECK(!pm.map_2m(0x200000, astia_test::arena_base + 0x800000, rw));
}

ASTIA_TEST(page_map_maps_higher_half) {
    auto pm = make_map();
    const astia::u64 virt = 0xffffffff80000000ull; // canonical, PML4 slot 511
    const astia::u64 phys = astia_test::arena_base + 0x300000;

    ASTIA_CHECK(pm.map_4k(virt, phys, rw));
    ASTIA_CHECK(pm.translate(virt) == phys);
}

ASTIA_TEST(page_map_rejects_misaligned_and_non_canonical) {
    auto pm = make_map();

    ASTIA_CHECK(!pm.map_4k(0x1234, astia_test::arena_base + 0x900000, rw));
    ASTIA_CHECK(!pm.map_4k(0x600000, astia_test::arena_base + 0x1234, rw));
    ASTIA_CHECK(!pm.map_2m(0x200000, astia_test::arena_base + 0x1000, rw));

    const astia::u64 non_canonical = 0x0001000000000000ull;
    ASTIA_CHECK(!pm.map_4k(non_canonical, astia_test::arena_base + 0xa00000, rw));
    ASTIA_CHECK(pm.translate(non_canonical) == page_map::invalid);
}

ASTIA_TEST(page_map_unmap_removes_4k_over_split) {
    auto pm = make_map();
    const astia::u64 base_phys = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(pm.map_2m(0x200000, base_phys, rw));
    ASTIA_CHECK(pm.map_4k(0x201000, astia_test::arena_base + 0x600000, rw));
    ASTIA_CHECK(pm.unmap(0x201000));
    ASTIA_CHECK(pm.translate(0x201000) == page_map::invalid);
    // Only that one page went away.
    ASTIA_CHECK(pm.translate(0x200000) == base_phys);
    ASTIA_CHECK(pm.translate(0x202000) == base_phys + 0x2000);
}

// --- parent pagemaps -------------------------------------------------------

ASTIA_TEST(page_map_child_shares_parent_kernel_half) {
    auto parent = make_map();
    const astia::u64 phys = astia_test::arena_base + 0x200000;
    ASTIA_CHECK(parent.map_4k(kvirt(0x400000), phys, rw));

    auto child = page_map::create(&parent);
    ASTIA_CHECK(child.valid());
    ASTIA_CHECK(child.parent() == &parent);
    // The child's hardware walk sees the parent's kernel mapping...
    ASTIA_CHECK(child.translate(kvirt(0x400000)) == phys);
    // ...while its own (user) half starts empty.
    ASTIA_CHECK(child.translate(0x400000) == page_map::invalid);
}

ASTIA_TEST(page_map_without_parent_sees_no_kernel_mappings) {
    auto parent = make_map();
    ASTIA_CHECK(parent.map_4k(kvirt(0x600000), astia_test::arena_base + 0x200000, rw));

    auto orphan = make_map(); // no parent: nothing shared
    ASTIA_CHECK(orphan.parent() == nullptr);
    ASTIA_CHECK(orphan.translate(kvirt(0x600000)) == page_map::invalid);
}

ASTIA_TEST(page_map_parent_fallback_for_late_kernel_maps) {
    auto parent = make_map();
    auto child = page_map::create(&parent);
    ASTIA_CHECK(child.valid());

    // Mapped on the parent *after* the child copied its (empty) kernel
    // half, so the child's own top-level slot knows nothing about it...
    const astia::u64 virt = kvirt(0x800000);
    const astia::u64 phys = astia_test::arena_base + 0x300000;
    ASTIA_CHECK(parent.map_4k(virt, phys, rw));

    // ...translate still finds it through the parent.
    ASTIA_CHECK(child.translate(virt) == phys);
    ASTIA_CHECK(child.translate(virt) == parent.translate(virt));
}

ASTIA_TEST(page_map_child_maps_only_its_own_half) {
    auto parent = make_map();
    auto child = page_map::create(&parent);
    const astia::u64 phys = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(child.map_4k(0x700000, phys, rw));
    ASTIA_CHECK(child.translate(0x700000) == phys);
    // The parent is untouched: child maps only touch the child.
    ASTIA_CHECK(parent.translate(0x700000) == page_map::invalid);
}

// --- recursive mapping -----------------------------------------------------

ASTIA_TEST(page_map_recursive_slot_points_at_own_root) {
    auto parent = make_map();
    ASTIA_CHECK(parent.map_4k(0x400000, astia_test::arena_base + 0x200000, rw));

    auto child = page_map::create(&parent);
    ASTIA_CHECK(child.valid());
    ASTIA_CHECK(parent.root() != child.root());

    // A walk that takes every level through the recursive slot ends up
    // reading the root page itself: PML4[slot] -> root -> ... -> root, so
    // the full-recursion address aliases the root table page. Each map's
    // alias must resolve to *its own* root, not the parent's.
    const astia::u64 slot = (astia::arch::recursive_base >> 39) & 0x1ff;
    const astia::u64 off = 0x123;
    const astia::u64 self = astia::arch::recursive_base + (slot << 30) + (slot << 21) +
                            (slot << 12) + off;

    ASTIA_CHECK(parent.translate(self) == parent.root() + off);
    ASTIA_CHECK(child.translate(self) == child.root() + off);
    ASTIA_CHECK(parent.translate(self) != child.root() + off);
}

ASTIA_TEST(page_map_recursive_window_is_reserved) {
    auto pm = make_map();
    const astia::u64 slot = (astia::arch::recursive_base >> 39) & 0x1ff;
    const astia::u64 full = astia::arch::recursive_base + (slot << 30) + (slot << 21) +
                            (slot << 12);

    // Nothing may be mapped on top of the self-referencing slot...
    ASTIA_CHECK(!pm.map_4k(astia::arch::recursive_base + 0x1000, astia_test::arena_base + 0x200000, rw));
    ASTIA_CHECK(!pm.map_2m(astia::arch::recursive_base + 0x200000, astia_test::arena_base + 0x400000, rw));
    ASTIA_CHECK(!pm.unmap(full));

    // ...but the window still translates through the self-entry: the
    // full-recursion address resolves to the root page at offset 0.
    ASTIA_CHECK(pm.translate(full) == pm.root());
}
