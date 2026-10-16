#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>

#include "arena.hpp"
#include "test.hpp"

namespace {

    using astia::error_t;
    using astia::mm::heap::alloc;
    using astia::mm::heap::alloc_zeroed;
    using astia::mm::heap::free;

    // The heap backs its window through map_fn; on the host the "window" is
    // plain memory that is already reachable, so the arch hook is a no-op.
    auto fake_map(astia::u64, astia::u64) -> bool { return true; }

    // heap::init() takes its backing frames from the kernel frame allocator;
    // point that at the arena, like the other substrate-bound tests do.
    auto init_frames() -> void {
        astia_test::map_arena();

        static bool ready = false;
        if (!ready) {
            astia::mm::memory_map map;
            map.add(astia_test::arena_base, astia_test::arena_size, astia::mm::memory_type::usable);
            astia::mm::kernel_frames().init(map);
            ready = true;
        }
    }

    alignas(16) astia::u8 region[64 * 1024];

} // namespace

// Must run before anything configures the heap.
ASTIA_TEST(heap_before_init_reports_enomem) {
    auto got = alloc(16);
    ASTIA_CHECK(!got);
    ASTIA_CHECK(got.error == error_t::enomem);
    ASTIA_CHECK(!astia::mm::heap::ready());
}

ASTIA_TEST(heap_configure_and_init) {
    // Poison the payload so both split-copies and zeroing are observable.
    for (astia::usize i = 0; i < sizeof(region); ++i) {
        region[i] = 0xaa;
    }
    init_frames();
    astia::mm::heap::configure(region, sizeof(region), fake_map);
    ASTIA_CHECK(astia::mm::heap::init());
    ASTIA_CHECK(astia::mm::heap::ready());
    // Capacity is the region minus one block header.
    ASTIA_CHECK(astia::mm::heap::capacity() > sizeof(region) - 64);
}

ASTIA_TEST(heap_alloc_round_trip) {
    auto got = alloc(100);
    ASTIA_CHECK(got);
    ASTIA_CHECK((reinterpret_cast<astia::u64>(got.value) % 16) == 0);
    // Plain alloc does not zero: the poison is still there.
    ASTIA_CHECK(got.value[0] == 0xaa);

    free(got.value);
}

ASTIA_TEST(heap_alloc_zeroed_clears_payload) {
    auto got = alloc_zeroed(64);
    ASTIA_CHECK(got);
    for (astia::usize i = 0; i < 64; ++i) {
        ASTIA_CHECK(got.value[i] == 0);
    }
    free(got.value);
}

ASTIA_TEST(heap_oom_is_an_error_not_a_panic) {
    auto got = alloc(sizeof(region) * 4);
    ASTIA_CHECK(!got);
    ASTIA_CHECK(got.error == error_t::enomem);

    auto zero = alloc(0);
    ASTIA_CHECK(!zero);
    ASTIA_CHECK(zero.error == error_t::einval);
}

ASTIA_TEST(heap_free_coalesces_blocks) {
    auto a = alloc(100);
    auto b = alloc(100);
    auto c = alloc(100);
    ASTIA_CHECK(a && b && c);

    free(a.value);
    free(b.value); // backward merge: a and b become one block

    // Only the merged a+b block can satisfy this; two 100-byte holes cannot.
    auto big = alloc(150);
    ASTIA_CHECK(big);
    ASTIA_CHECK(big.value == a.value);

    free(big.value);
    free(c.value);
}

ASTIA_TEST(heap_ignores_foreign_and_double_frees) {
    int on_stack = 0;
    free(&on_stack); // outside the region: ignored
    free(nullptr);   // ignored

    auto got = alloc(32);
    ASTIA_CHECK(got);
    free(got.value);
    const auto used_before = astia::mm::heap::used();
    free(got.value); // double free: ignored
    ASTIA_CHECK(astia::mm::heap::used() == used_before);
}
