#include "arena.hpp"
#include "test.hpp"
#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/phys_region.hpp>
#include <astia/mm/pinned_buffer.hpp>

namespace {

    using astia::mm::phys_region;

    // The heap's arch hook is a no-op on the host: the window is
    // already reachable memory.
    bool fake_map(astia::u64, astia::u64) {
        return true;
    }

    // phys_region takes its frames from the kernel frame allocator and
    // its objects from the heap; point both at the arena like the
    // other substrate-bound tests do, and report the free-frame count
    // so callers can assert full reclamation.
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

} // namespace

ASTIA_TEST(phys_region_create_adopts_frames) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);
    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(region->pages() == 4);
    ASTIA_CHECK(region->references() == 1);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_unique_until_shared) {
    init_world();
    auto* region = phys_region::create(2, true);
    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(region->uniquely_held(0));
    region->addref(); // a fork would do exactly this
    ASTIA_CHECK(!region->uniquely_held(0));
    ASTIA_CHECK(region->references() == 2);
    region->release();
    ASTIA_CHECK(region->uniquely_held(0)); // sole sharer again
    region->release();
}

ASTIA_TEST(phys_region_split_page_copies_and_tracks) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);
    ASTIA_CHECK(region != nullptr);
    auto* bytes = static_cast<astia::u8*>(region->page_direct(1));
    for (astia::usize b = 0; b < phys_region::frame_size; ++b) {
        bytes[b] = static_cast<astia::u8>(b & 0xFF);
    }

    region->addref(); // shared between two address spaces
    auto* child = region->split_page(1);
    ASTIA_CHECK(child != nullptr);
    ASTIA_CHECK(child->pages() == 1);
    ASTIA_CHECK(child->references() == 1);
    const auto* copy = static_cast<const astia::u8*>(child->page_direct(0));
    ASTIA_CHECK(copy[0] == 0 && copy[17] == 17 && copy[4095] == 255);
    // The copier now maps the child, not the parent page: one sharer
    // left on page 1 — but page 0 is still shared.
    ASTIA_CHECK(region->uniquely_held(1));
    ASTIA_CHECK(!region->uniquely_held(0));

    // A second copier of the same page: nobody maps the parent page.
    auto* second = region->split_page(1);
    ASTIA_CHECK(second != nullptr);
    ASTIA_CHECK(!region->uniquely_held(1));
    second->release(); // the second copier exits
    // The remaining copier owns a child page; the parent page has no
    // mapping left, so it is not a writable in-place candidate.
    ASTIA_CHECK(!region->uniquely_held(1));

    child->release();
    region->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_reclaims_last_unmapped_page) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);
    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);

    // Two address spaces share the four-page range.  Removing page 1
    // from one leaves its frame alive; removing it from the second
    // returns exactly that frame while the neighbours remain allocated.
    ASTIA_CHECK(region->share_range(0, 4));
    ASTIA_CHECK(region->unmap_range(1, 1));
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);
    ASTIA_CHECK(region->unmap_range(1, 1));
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 3);

    region->release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(phys_region_parent_death_detaches_children) {
    const auto before = init_world();
    auto*      region = phys_region::create(1, true);
    ASTIA_CHECK(region != nullptr);
    auto* child = region->split_page(0);
    ASTIA_CHECK(child != nullptr);

    region->release(); // last reference: the parent dies first
    // The child owns its own frames and needs no parent: it must stay // fully usable,
    // and able to CoW again.
    auto* byte = static_cast<astia::u8*>(child->page_direct(0));
    byte[0]    = 0x77;
    ASTIA_CHECK(byte[0] == 0x77);
    auto* grandchild = child->split_page(0);
    ASTIA_CHECK(grandchild != nullptr);
    grandchild->release();
    child->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_holds_frames_across_vma_release) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);
    ASTIA_CHECK(region != nullptr);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 4);

    astia::mm::pinned_buffer pin;
    ASTIA_CHECK(pin.acquire(region, 1, 2));
    ASTIA_CHECK(pin.valid());
    ASTIA_CHECK(pin.pages() == 2);
    ASTIA_CHECK(region->references() == 2); // coverage + allocation reference

    // The mapping goes away while the operation is in flight.  The two frames
    // it referenced must survive — and so must the region object that holds
    // the counters the pin will later decrement.  (Holding only the coverage
    // would free the object here and leave release() writing into freed
    // memory.)
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    pin.release();
    ASTIA_CHECK(!pin.valid());
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);

    pin.release(); // idempotent, and safe on an empty buffer
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_exposes_one_contiguous_window) {
    const auto before = init_world();
    auto*      region = phys_region::create(4, true);
    ASTIA_CHECK(region != nullptr);

    astia::mm::pinned_buffer pin;
    ASTIA_CHECK(pin.acquire(region, 1, 3));

    // A region is a contiguous frame run and the direct map is linear, so the
    // pinned range is a single kernel window.  That is the property the ring
    // leans on to hand a device one base address instead of a scatter list.
    auto* base = static_cast<astia::u8*>(pin.direct(0));
    ASTIA_CHECK(base == region->page_direct(1));
    for (astia::usize page = 0; page < pin.pages(); ++page) {
        ASTIA_CHECK(pin.direct(page) == base + page * phys_region::frame_size);
        ASTIA_CHECK(pin.phys(page) ==
                    region->base() + (1 + page) * phys_region::frame_size);
    }

    pin.release();
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}

ASTIA_TEST(pinned_buffer_rejects_ranges_outside_the_region) {
    const auto before = init_world();
    auto*      region = phys_region::create(2, true);
    ASTIA_CHECK(region != nullptr);

    astia::mm::pinned_buffer pin;
    ASTIA_CHECK(!pin.acquire(nullptr, 0, 1));
    ASTIA_CHECK(!pin.acquire(region, 0, 0));
    ASTIA_CHECK(!pin.acquire(region, 2, 1)); // first page past the end
    ASTIA_CHECK(!pin.acquire(region, 1, 2)); // runs off the end
    ASTIA_CHECK(!pin.valid());

    // A rejected acquire references nothing and takes nothing.
    ASTIA_CHECK(region->references() == 1);
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before - 2);

    pin.release(); // empty: a no-op
    region->release();
    ASTIA_CHECK(astia::mm::kernel_frames().available_frames() == before);
}
