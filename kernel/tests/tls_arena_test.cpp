#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/proc/user_layout.hpp>

#include "../src/proc/tls_arena.hpp"
#include "arena.hpp"
#include "test.hpp"

namespace {

    using astia::mm::phys_region;

    bool fake_map(astia::u64, astia::u64) {
        return true;
    }

    astia::usize init_world() {
        astia_test::map_arena();

        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;
            map.add(astia_test::arena_base, astia_test::arena_size,
                    astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);

            static astia::u8 region[64 * 1024];
            astia::mm::heap::configure(region, sizeof(region), fake_map);
            astia::mm::heap::init();
            ready = true;
        }
        return astia::mm::kernel_frames().available_frames();
    }

    constexpr astia::usize reserve_pages =
        astia::proc::user_dso_tls_reserve / phys_region::frame_size;

} // namespace

ASTIA_TEST(tls_arena_reserve_costs_no_frames_until_touched) {
    const auto before = init_world();

    astia::proc::tls_arena layout{};
    ASTIA_CHECK(astia::proc::measure_tls_arena(1, layout));
    ASTIA_CHECK(layout.reserve_pages == reserve_pages);
    ASTIA_CHECK(layout.payload_pages == 1);
    ASTIA_CHECK(layout.total_pages == reserve_pages + 1);

    auto* region = astia::proc::create_tls_arena(layout);
    ASTIA_CHECK(region != nullptr);
    if (region == nullptr)
        return;
    ASTIA_CHECK(region->pages() == layout.total_pages);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 1);

    for (astia::usize page = 0; page < layout.reserve_pages; ++page) {
        ASTIA_CHECK(region->page_base(page) == astia::mm::frame_allocator::invalid_frame);
        ASTIA_CHECK(region->page_direct(page) == nullptr);
    }
    const auto* payload =
        static_cast<const astia::u8*>(region->page_direct(layout.reserve_pages));
    ASTIA_CHECK(payload != nullptr);
    if (payload == nullptr)
        return;
    for (astia::usize i = 0; i < 64; ++i)
        ASTIA_CHECK(payload[i] == 0);

    bool created = false;
    ASTIA_CHECK(region->ensure_page(0, true, &created));
    ASTIA_CHECK(created);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(tls_arena_payload_copy_spans_lazy_pages) {
    init_world();

    astia::proc::tls_arena layout{};
    ASTIA_CHECK(astia::proc::measure_tls_arena(2, layout));
    auto* region = astia::proc::create_tls_arena(layout);
    ASTIA_CHECK(region != nullptr);
    if (region == nullptr)
        return;

    astia::u8 source[phys_region::frame_size + 8];
    for (astia::usize i = 0; i < sizeof(source); ++i)
        source[i] = static_cast<astia::u8>(i);

    // Start eight bytes before the page boundary so the copy crosses from
    // the first payload frame into the second.
    ASTIA_CHECK(astia::proc::write_tls_payload(
        *region, layout, phys_region::frame_size - 8,
        astia::span<const astia::u8>(source, sizeof(source))));

    const auto* first =
        static_cast<const astia::u8*>(region->page_direct(layout.reserve_pages));
    const auto* second =
        static_cast<const astia::u8*>(region->page_direct(layout.reserve_pages + 1));
    ASTIA_CHECK(first != nullptr);
    if (first == nullptr)
        return;
    ASTIA_CHECK(second != nullptr);
    if (second == nullptr)
        return;
    for (astia::usize i = 0; i < 8; ++i)
        ASTIA_CHECK(first[phys_region::frame_size - 8 + i] == source[i]);
    for (astia::usize i = 0; i < phys_region::frame_size; ++i)
        ASTIA_CHECK(second[i] == source[8 + i]);

    region->release();
}
