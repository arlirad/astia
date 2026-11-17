#include "../src/proc/exec_rollback.hpp"
#include "support/arena.hpp"
#include "test.hpp"
#include <astia/arch/page_map.hpp>
#include <astia/mm/address_space.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::arch {
    page_map* kernel_page_map() {
        return nullptr;
    }
}

namespace {

    struct state {
        astia::mm::address_space space;
        astia::u64               region_frames = 0;
        astia::u32               unmaps = 0;
        astia::u32               frame_releases = 0;
        astia::u32               region_releases = 0;
        astia::u64               last_address = 0;
        astia::usize             last_pages = 0;
    };

    void init_frames() {
        astia_test::map_arena();
        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;
            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);
            ready = true;
        }
    }

    void unmap(void* context, astia::u64 address, astia::usize pages) {
        auto& value = *static_cast<state*>(context);
        for (astia::usize i = 0; i < pages; ++i)
            ASTIA_CHECK(value.space.unmap(address + i * astia::mm::page_size));
        value.last_address = address;
        value.last_pages = pages;
        value.unmaps += static_cast<astia::u32>(pages);
    }

    void release_frames(void* context, astia::u64 frames, astia::u64 pages) {
        auto& value = *static_cast<state*>(context);
        for (astia::usize i = 0; i < value.last_pages; ++i)
            ASTIA_CHECK(value.space.translate(value.last_address +
                                              i * astia::mm::page_size) ==
                        astia::mm::address_space::invalid);
        astia::mm::kernel_frames().release(frames, pages);
        ++value.frame_releases;
    }

    void release_region(void* context) {
        auto& value = *static_cast<state*>(context);
        for (astia::usize i = 0; i < value.last_pages; ++i)
            ASTIA_CHECK(value.space.translate(value.last_address +
                                              i * astia::mm::page_size) ==
                        astia::mm::address_space::invalid);
        astia::mm::kernel_frames().release(value.region_frames, 2);
        ++value.region_releases;
    }

    void map_two_pages(state& value, astia::u64 address, astia::u64 frames) {
        const auto flags = astia::mm::mapping_flags::present |
                           astia::mm::mapping_flags::writable |
                           astia::mm::mapping_flags::user;
        ASTIA_CHECK(value.space.map_4k(address, frames, flags));
        ASTIA_CHECK(value.space.map_4k(address + astia::mm::page_size,
                                       frames + astia::mm::page_size, flags));
        ASTIA_CHECK(value.space.translate(address) == frames);
        ASTIA_CHECK(value.space.translate(address + astia::mm::page_size) ==
                    frames + astia::mm::page_size);
    }

} // namespace

ASTIA_TEST(exec_rollback_sync_path_clears_real_page_table_before_release) {
    init_frames();
    state value{astia::mm::address_space::create()};
    ASTIA_CHECK(value.space.valid());
    const astia::u64 frames = astia::mm::kernel_frames().alloc(2);
    ASTIA_CHECK(frames != astia::mm::frame_allocator::invalid_frame);
    map_two_pages(value, 0x400000, frames);

    bool mapped = true;
    astia::proc::detail::rollback_mapping_entry entry{0x400000, 2, frames, nullptr,
                                                       &mapped, true};
    astia::proc::detail::rollback_registered_mappings(
        &value, &entry, 1, unmap, release_frames, release_region);
    ASTIA_CHECK(value.space.translate(0x400000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(value.space.translate(0x401000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(value.unmaps == 2);
    ASTIA_CHECK(value.frame_releases == 1);
    ASTIA_CHECK(value.region_releases == 0);
    ASTIA_CHECK(!mapped);
}

ASTIA_TEST(exec_rollback_stream_path_clears_real_page_table_before_region_release) {
    init_frames();
    state value{astia::mm::address_space::create()};
    ASTIA_CHECK(value.space.valid());
    value.region_frames = astia::mm::kernel_frames().alloc(2);
    ASTIA_CHECK(value.region_frames != astia::mm::frame_allocator::invalid_frame);
    map_two_pages(value, 0x500000, value.region_frames);

    bool mapped = true;
    astia::proc::detail::rollback_mapping_entry entry{0x500000, 2, 0, &value, &mapped,
                                                       true};
    astia::proc::detail::rollback_registered_mappings(
        &value, &entry, 1, unmap, release_frames, release_region);
    ASTIA_CHECK(value.space.translate(0x500000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(value.space.translate(0x501000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(value.unmaps == 2);
    ASTIA_CHECK(value.frame_releases == 0);
    ASTIA_CHECK(value.region_releases == 1);
    ASTIA_CHECK(!mapped);
}

ASTIA_TEST(exec_rollback_multi_entry_is_idempotent) {
    init_frames();
    state value{astia::mm::address_space::create()};
    ASTIA_CHECK(value.space.valid());
    const astia::u64 first = astia::mm::kernel_frames().alloc(2);
    const astia::u64 second = astia::mm::kernel_frames().alloc(2);
    ASTIA_CHECK(first != astia::mm::frame_allocator::invalid_frame);
    ASTIA_CHECK(second != astia::mm::frame_allocator::invalid_frame);
    map_two_pages(value, 0x600000, first);
    map_two_pages(value, 0x700000, second);
    bool mapped[2] = {true, true};
    astia::proc::detail::rollback_mapping_entry entries[2]{
        {0x600000, 2, first, nullptr, &mapped[0], true},
        {0x700000, 2, second, nullptr, &mapped[1], true},
    };
    astia::proc::detail::rollback_registered_mappings(
        &value, entries, 2, unmap, release_frames, release_region);
    astia::proc::detail::rollback_registered_mappings(
        &value, entries, 2, unmap, release_frames, release_region);
    ASTIA_CHECK(value.unmaps == 4);
    ASTIA_CHECK(value.frame_releases == 2);
    ASTIA_CHECK(!mapped[0] && !mapped[1]);
    ASTIA_CHECK(value.space.translate(0x600000) == astia::mm::address_space::invalid);
    ASTIA_CHECK(value.space.translate(0x700000) == astia::mm::address_space::invalid);
}
