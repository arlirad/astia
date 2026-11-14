#include "arena.hpp"
#include "test.hpp"
#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/memory_map.hpp>

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

} // namespace

ASTIA_TEST(address_space_validates_user_ranges_and_permissions) {
    auto native = make_map();
    auto space  = astia::mm::address_space::view(&native);
    const auto phys = astia_test::arena_base + 0x200000;

    ASTIA_CHECK(space.valid());
    ASTIA_CHECK(space.valid_user_range(0x400001, 0x1fff));
    ASTIA_CHECK(!space.valid_user_range(0, 1));
    ASTIA_CHECK(!space.valid_user_range(astia::arch::kernel_base, 1));
    ASTIA_CHECK(!space.valid_user_range(astia::arch::kernel_base - 1, 2));
    ASTIA_CHECK(space.map_4k(0x400000, phys, user_rw));
    ASTIA_CHECK(space.validate(0x400001, 0x100, astia::mm::access::read));
    ASTIA_CHECK(space.validate(0x400001, 0x100, astia::mm::access::write));
    ASTIA_CHECK(!space.validate(0x400001, 0x100, astia::mm::access::execute));
    ASTIA_CHECK(space.protect_4k(
        0x400000, astia::mm::mapping_flags::present | astia::mm::mapping_flags::user));
    ASTIA_CHECK(!space.validate(0x400000, 1, astia::mm::access::write));
}

ASTIA_TEST(address_space_transaction_rolls_back_partial_mapping) {
    auto native = make_map();
    auto space  = astia::mm::address_space::view(&native);
    const auto phys = astia_test::arena_base + 0x300000;

    {
        astia::mm::mapping_transaction transaction(space, 0x500000, 2);
        ASTIA_CHECK(transaction.map_4k(0x500000, phys, user_rw));
        ASTIA_CHECK(!transaction.map_4k(0x501000, phys + 1, user_rw));
    }
    ASTIA_CHECK(space.translate(0x500000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(space.translate(0x501000) == astia::mm::address_space::invalid);
}

ASTIA_TEST(address_space_transaction_commit_keeps_mappings) {
    auto native = make_map();
    auto space  = astia::mm::address_space::view(&native);
    const auto phys = astia_test::arena_base + 0x400000;

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

ASTIA_TEST(address_space_create_uses_parent_and_owns_backend) {
    auto native = make_map();
    auto parent = astia::mm::address_space::view(&native);
    auto child  = astia::mm::address_space::create(&parent);
    const auto phys = astia_test::arena_base + 0x500000;

    ASTIA_CHECK(child.valid());
    ASTIA_CHECK(child.map_4k(0x700000, phys, user_rw));
    ASTIA_CHECK(child.translate(0x700000) == phys);
    ASTIA_CHECK(native.translate(0x700000) == astia::arch::page_map::invalid);
}
