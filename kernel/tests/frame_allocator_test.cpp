#include <astia/mm/frame_allocator.hpp>

#include "arena.hpp"
#include "test.hpp"

namespace {
    using astia::mm::frame_allocator;
    using astia::mm::frame_flags;
    using astia::mm::memory_map;
    using astia::mm::memory_type;

    // A fresh allocator per test, built from the shared arena window.
    void init_with_arena(frame_allocator& alloc) {
        astia_test::map_arena();

        memory_map map;

        map.add(astia_test::arena_base, astia_test::arena_size, memory_type::usable);
        alloc.init(map);
    }

    constexpr astia::u64 frame_size = 4096;
} // namespace

ASTIA_TEST(frame_allocator_tracks_usable_extent) {
    frame_allocator alloc;

    init_with_arena(alloc);

    ASTIA_CHECK(alloc.initialized());
    ASTIA_CHECK(alloc.total_frames() == astia_test::arena_end / frame_size);
    // All 8 MiB of the arena are usable, minus the 2-frame kernel image and
    // the 2 frames the bitmap needs (544 words * 8 bytes).
    ASTIA_CHECK(alloc.available_frames() == astia_test::arena_size / frame_size - 4);
    ASTIA_CHECK(!frame_allocator().initialized());
}

ASTIA_TEST(frame_allocator_never_returns_reserved_frames) {
    frame_allocator alloc;

    init_with_arena(alloc);

    const astia::u64 frame = alloc.alloc(1);

    ASTIA_CHECK(frame != frame_allocator::invalid_frame);
    ASTIA_CHECK((frame % frame_size) == 0);
    // Past the kernel image (and the bitmap parked right after it).
    ASTIA_CHECK(frame >= astia_test::arena_base + astia_test::kernel_image_size);
    ASTIA_CHECK(frame < astia_test::arena_end);

    // And, being a real host address, it is safe to touch.
    *reinterpret_cast<volatile astia::u64*>(frame) = 0xabc;
}

ASTIA_TEST(frame_allocator_allocates_contiguous_run) {
    frame_allocator alloc;

    init_with_arena(alloc);

    const astia::u64 first = alloc.alloc(4);

    ASTIA_CHECK(first != frame_allocator::invalid_frame);

    // The next single frame starts right after the four-frame run.
    const astia::u64 next = alloc.alloc(1);

    ASTIA_CHECK(next == first + 4 * frame_size);
}

ASTIA_TEST(frame_allocator_tracks_free_count) {
    frame_allocator alloc;

    init_with_arena(alloc);

    const astia::usize before = alloc.available_frames();
    const astia::u64   frame  = alloc.alloc(3);

    ASTIA_CHECK(frame != frame_allocator::invalid_frame);
    ASTIA_CHECK(alloc.available_frames() == before - 3);

    alloc.release(frame, 3);
    ASTIA_CHECK(alloc.available_frames() == before);
    // The freed run is the first free run again.
    ASTIA_CHECK(alloc.alloc(3) == frame);
}

ASTIA_TEST(frame_allocator_zeroes_on_request) {
    frame_allocator alloc;

    init_with_arena(alloc);

    const astia::u64 frame = alloc.alloc(1);
    auto*            words = reinterpret_cast<astia::u64*>(frame);

    for (astia::usize i = 0; i < frame_size / sizeof(astia::u64); ++i) {
        words[i] = 0xdeadbeefcafef00dull;
    }

    alloc.release(frame, 1);

    const astia::u64 again = alloc.alloc(1, frame_flags::zeroed);

    ASTIA_CHECK(again == frame);

    bool zeroed = true;

    for (astia::usize i = 0; i < frame_size / sizeof(astia::u64); ++i) {
        zeroed = zeroed && (words[i] == 0);
    }

    ASTIA_CHECK(zeroed);
}

ASTIA_TEST(frame_allocator_rejects_impossible_requests) {
    frame_allocator alloc;

    init_with_arena(alloc);

    ASTIA_CHECK(alloc.alloc(0) == frame_allocator::invalid_frame);
    ASTIA_CHECK(alloc.alloc(alloc.available_frames() + 1) ==
                frame_allocator::invalid_frame);

    // Even a request that fits exactly is satisfied.
    const astia::usize free_frames = alloc.available_frames();

    ASTIA_CHECK(alloc.alloc(free_frames) != frame_allocator::invalid_frame);
    ASTIA_CHECK(alloc.available_frames() == 0);
}
