#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/memory_map.hpp>

#include "arena.hpp"
#include "test.hpp"

namespace {
    astia::arch::page_map make_map() {
        astia_test::map_arena();

        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;

            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);

            ready = true;
        }

        return astia::arch::page_map::create();
    }

    constexpr astia::mm::mapping_flags user_rw =
        astia::mm::mapping_flags::present | astia::mm::mapping_flags::writable |
        astia::mm::mapping_flags::user | astia::mm::mapping_flags::no_execute;

    constexpr astia::mm::mapping_flags user_read_only =
        astia::mm::mapping_flags::present | astia::mm::mapping_flags::user |
        astia::mm::mapping_flags::no_execute;

    struct modelled_tlb_entry {
        astia::paddr_t root     = 0;
        astia::vaddr_t virt     = 0;
        astia::paddr_t phys     = 0;
        bool           valid    = false;
        bool           writable = false;
    };

    modelled_tlb_entry modelled_tlb[2];
    astia::u32         modelled_invalidations = 0;

    void reset_tlb_model() {
        modelled_tlb[0]        = {};
        modelled_tlb[1]        = {};
        modelled_invalidations = 0;
    }

    void fill_model_tlb(astia::u32 cpu, const astia::arch::page_map& map,
                        astia::vaddr_t virt) {
        modelled_tlb_entry& entry = modelled_tlb[cpu];

        entry.root     = map.root();
        entry.virt     = virt;
        entry.phys     = map.translate(virt);
        entry.valid    = entry.phys != astia::arch::page_map::invalid;
        entry.writable = entry.valid && map.writable(virt);
    }
} // namespace

extern "C" void astia_host_tlb_invalidate(astia::paddr_t root, astia::vaddr_t virt) {
    ++modelled_invalidations;

    for (modelled_tlb_entry& entry : modelled_tlb) {
        if (entry.valid && entry.root == root && entry.virt == virt)
            entry.valid = false;
    }
}

ASTIA_TEST(cow_break_reaches_other_cpus_tlb) {
    auto       native = make_map();
    auto       parent = astia::mm::address_space::view(&native);
    auto       child  = astia::mm::address_space::create(&parent);
    const auto phys   = astia_test::arena_base + 0x200000;
    const auto virt   = static_cast<astia::vaddr_t>(0xb00000);

    ASTIA_CHECK(parent.map_4k(virt, phys, user_rw));

    // Two CPUs run the parent and cache its writable translation.
    reset_tlb_model();
    fill_model_tlb(0, native, virt);
    fill_model_tlb(1, native, virt);
    ASTIA_CHECK(modelled_tlb[1].valid);
    ASTIA_CHECK(modelled_tlb[1].writable);

    // fork(): the child gets a fresh read-only PTE (nothing cached to drop),
    // then the parent's existing writable PTE is narrowed.
    ASTIA_CHECK(child.map_4k(virt, phys, user_read_only));
    ASTIA_CHECK(modelled_invalidations == 0);

    ASTIA_CHECK(parent.map_4k(virt, phys, user_read_only));
    ASTIA_CHECK(modelled_invalidations == 1);
    ASTIA_CHECK(!modelled_tlb[0].valid);
    ASTIA_CHECK(!modelled_tlb[1].valid);

    // The other CPU refills on its next access and sees the narrowed entry.
    fill_model_tlb(1, native, virt);
    ASTIA_CHECK(modelled_tlb[1].valid);
    ASTIA_CHECK(modelled_tlb[1].phys == phys);
    ASTIA_CHECK(!modelled_tlb[1].writable);
}

ASTIA_TEST(protect_and_unmap_reach_other_cpus_tlb) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x300000;
    const auto virt   = static_cast<astia::vaddr_t>(0xd00000);

    ASTIA_CHECK(space.map_4k(virt, phys, user_rw));

    reset_tlb_model();
    fill_model_tlb(1, native, virt);

    ASTIA_CHECK(space.protect_4k(virt, user_read_only));
    ASTIA_CHECK(modelled_invalidations == 1);
    ASTIA_CHECK(!modelled_tlb[1].valid);

    fill_model_tlb(1, native, virt);
    ASTIA_CHECK(!modelled_tlb[1].writable);

    reset_tlb_model();
    fill_model_tlb(1, native, virt);
    ASTIA_CHECK(space.unmap(virt));
    ASTIA_CHECK(modelled_invalidations == 1);
    ASTIA_CHECK(!modelled_tlb[1].valid);
}

ASTIA_TEST(fresh_mapping_skips_remote_invalidation) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x400000;

    reset_tlb_model();

    ASTIA_CHECK(space.map_4k(0xe00000, phys, user_rw));
    ASTIA_CHECK(modelled_invalidations == 0);

    ASTIA_CHECK(space.map_4k(0xe00000, phys, user_rw));
    ASTIA_CHECK(modelled_invalidations == 0);
}

ASTIA_TEST(address_space_validates_user_ranges_and_permissions) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(space.valid());
    ASTIA_CHECK(space.valid_user_range(0x400001, 0x1fff));
    ASTIA_CHECK(!space.valid_user_range(0, 1));
    ASTIA_CHECK(!space.valid_user_range(astia::arch::kernel_base, 1));
    ASTIA_CHECK(!space.valid_user_range(astia::arch::kernel_base - 1, 2));
    ASTIA_CHECK(!space.valid_user_range(0x0000800000000000ull, 1));
    ASTIA_CHECK(!space.valid_user_range(0x0001000000000000ull, 1));
    ASTIA_CHECK(!space.valid_user_range(~astia::u64{0} - 1, 4));
    ASTIA_CHECK(space.valid_user_range(0, 0));
    ASTIA_CHECK(space.map_4k(0x400000, phys, user_rw));
    ASTIA_CHECK(space.validate(0x400001, 0x100, astia::mm::access::read));
    ASTIA_CHECK(space.validate(0x400001, 0x100, astia::mm::access::write));
    ASTIA_CHECK(!space.validate(0x400001, 0x100, astia::mm::access::execute));
    ASTIA_CHECK(space.protect_4k(0x400000, astia::mm::mapping_flags::present |
                                               astia::mm::mapping_flags::user));
    ASTIA_CHECK(!space.validate(0x400000, 1, astia::mm::access::write));
}

ASTIA_TEST(address_space_transaction_rolls_back_partial_mapping) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x300000;

    {
        astia::mm::mapping_transaction transaction(space, 0x500000, 2);

        ASTIA_CHECK(transaction.map_4k(0x500000, phys, user_rw));
        ASTIA_CHECK(!transaction.map_4k(0x501000, phys + 1, user_rw));
    }

    ASTIA_CHECK(space.translate(0x500000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(space.translate(0x501000) == astia::mm::address_space::invalid);
}

ASTIA_TEST(address_space_transaction_commit_keeps_mappings) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x400000;

    {
        astia::mm::mapping_transaction transaction(space, 0x600000, 2);

        ASTIA_CHECK(transaction.map_4k(0x600000, phys, user_rw));
        ASTIA_CHECK(transaction.map_4k(0x601000, phys + 0x1000, user_rw));
        transaction.commit();
    }

    ASTIA_CHECK(space.translate(0x600000) == phys);
    ASTIA_CHECK(space.translate(0x601000) == phys + 0x1000);
    ASTIA_CHECK(space.unmap(0x600000));
    ASTIA_CHECK(space.unmap(0x601000));
}

ASTIA_TEST(address_space_transaction_preserves_existing_mapping_on_failure) {
    auto       native = make_map();
    auto       space  = astia::mm::address_space::view(&native);
    const auto phys   = astia_test::arena_base + 0x600000;

    ASTIA_CHECK(space.map_4k(0x800000, phys, user_rw));

    {
        astia::mm::mapping_transaction transaction(space, 0x800000, 2);

        ASTIA_CHECK(!transaction.map_4k(0x800000, phys + 0x1000, user_rw));
    }

    ASTIA_CHECK(space.translate(0x800000) == phys);
    ASTIA_CHECK(space.translate(0x801000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(space.unmap(0x800000));
}

ASTIA_TEST(address_space_create_uses_parent_and_owns_backend) {
    auto       native = make_map();
    auto       parent = astia::mm::address_space::view(&native);
    auto       child  = astia::mm::address_space::create(&parent);
    const auto phys   = astia_test::arena_base + 0x500000;

    ASTIA_CHECK(child.valid());
    ASTIA_CHECK(child.map_4k(0x700000, phys, user_rw));
    ASTIA_CHECK(child.translate(0x700000) == phys);
    ASTIA_CHECK(native.translate(0x700000) == astia::arch::page_map::invalid);
}

ASTIA_TEST(address_space_fork_child_mapping_isolated_from_parent) {
    auto       native      = make_map();
    auto       parent      = astia::mm::address_space::view(&native);
    auto       child       = astia::mm::address_space::create(&parent);
    const auto parent_phys = astia_test::arena_base + 0x700000;
    const auto child_phys  = astia_test::arena_base + 0x701000;

    ASTIA_CHECK(parent.map_4k(0x900000, parent_phys, user_rw));
    ASTIA_CHECK(child.map_4k(0x900000, child_phys, user_rw));
    ASTIA_CHECK(parent.translate(0x900000) == parent_phys);
    ASTIA_CHECK(child.translate(0x900000) == child_phys);
    ASTIA_CHECK(child.unmap(0x900000));
    ASTIA_CHECK(parent.translate(0x900000) == parent_phys);
    ASTIA_CHECK(child.translate(0x900000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(parent.unmap(0x900000));
}

ASTIA_TEST(address_space_exec_replaces_image_without_aliasing) {
    auto       native   = make_map();
    auto       parent   = astia::mm::address_space::view(&native);
    auto       old      = astia::mm::address_space::create(&parent);
    auto       image    = astia::mm::address_space::create(&parent);
    const auto old_phys = astia_test::arena_base + 0x800000;
    const auto new_phys = astia_test::arena_base + 0x801000;

    ASTIA_CHECK(old.map_4k(0xa00000, old_phys, user_rw));
    ASTIA_CHECK(image.map_4k(0xa00000, new_phys, user_rw));

    old = static_cast<astia::mm::address_space&&>(image);

    ASTIA_CHECK(old.translate(0xa00000) == new_phys);
    ASTIA_CHECK(image.translate(0xa00000) == astia::mm::address_space::invalid);
}
