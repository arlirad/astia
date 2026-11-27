#include <astia/mm/memory_map.hpp>

#include "test.hpp"

ASTIA_TEST(memory_map_add_and_index) {
    astia::mm::memory_map map;

    ASTIA_CHECK(map.count() == 0);

    map.add(0x100000, 0x400000, astia::mm::memory_type::usable);
    map.add(0, 0xa0000, astia::mm::memory_type::reserved);

    ASTIA_CHECK(map.count() == 2);
    ASTIA_CHECK(map[0].base == 0x100000);
    ASTIA_CHECK(map[0].size == 0x400000);
    ASTIA_CHECK(map[0].type == astia::mm::memory_type::usable);
    ASTIA_CHECK(map[1].base == 0);
    ASTIA_CHECK(map[1].type == astia::mm::memory_type::reserved);
}

ASTIA_TEST(memory_map_iteration) {
    astia::mm::memory_map map;

    map.add(1, 1, astia::mm::memory_type::usable);
    map.add(2, 2, astia::mm::memory_type::reserved);

    astia::u64 total = 0;

    for (const auto& region : map) {
        total += region.size;
    }

    ASTIA_CHECK(total == 3);
}

ASTIA_TEST(memory_map_ignores_overflow) {
    astia::mm::memory_map map;

    for (astia::usz i = 0; i < astia::mm::memory_map::max_regions + 10; ++i) {
        map.add(i * 4096, 4096, astia::mm::memory_type::usable);
    }

    ASTIA_CHECK(map.count() == astia::mm::memory_map::max_regions);
}
