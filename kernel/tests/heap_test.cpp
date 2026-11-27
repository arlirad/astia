#include <astia/mm/frame_allocator.hpp>
#include <astia/mm/heap.hpp>
#include <astia/mm/share.hpp>

#include "arena.hpp"
#include "heap_internal.hpp"
#include "test.hpp"
#include <sys/wait.h>

#include <signal.h>
#include <unistd.h>

namespace {
    using astia::error_t;
    using astia::mm::heap::alloc;
    using astia::mm::heap::alloc_zeroed;
    using astia::mm::heap::free;

    // The heap backs its window through map_fn; on the host the "window" is
    // plain memory that is already reachable, so the arch hook is a no-op.
    bool fake_map(astia::u64, astia::u64) {
        return true;
    }

    // heap::init() takes its backing frames from the kernel frame allocator;
    // point that at the arena, like the other substrate-bound tests do.
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
    for (astia::usz i = 0; i < sizeof(region); ++i) {
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

    for (astia::usz i = 0; i < 64; ++i) {
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

namespace {
    struct shared_probe {
        int* destroyed;

        ~shared_probe() {
            ++*destroyed;
        }
    };
}

ASTIA_TEST(shptr_share_releases_value_when_control_block_allocation_fails) {
    int destroyed = 0;

    const astia::usz used_before = astia::mm::heap::used();

    astia_test::fail_next_allocation();

    auto shared = astia::mm::share<shared_probe>(&destroyed);

    ASTIA_CHECK(!shared);
    ASTIA_CHECK(destroyed == 1);
    ASTIA_CHECK(astia::mm::heap::used() == used_before);
}

namespace {
    namespace heap_detail = astia::mm::heap::detail;

    alignas(16) astia::u8 validation_region[1024];

    constexpr astia::usz validation_split = sizeof(validation_region) / 2;

    heap_detail::window validation_window() {
        return {validation_region, sizeof(validation_region)};
    }

    heap_detail::block* validation_block(astia::usz offset) {
        return reinterpret_cast<heap_detail::block*>(validation_region + offset);
    }

    // Build a valid pair: first -> second, both covering the window exactly.
    heap_detail::block* validation_pair() {
        for (astia::usz i = 0; i < sizeof(validation_region); ++i) {
            validation_region[i] = 0;
        }

        auto* first  = validation_block(0);
        auto* second = validation_block(validation_split);

        first->size = validation_split - sizeof(heap_detail::block);
        first->next = second;
        second->size =
            sizeof(validation_region) - validation_split - sizeof(heap_detail::block);
        second->prev = first;

        return first;
    }
} // namespace

ASTIA_TEST(heap_validation_accepts_contiguous_pair) {
    auto*      first  = validation_pair();
    auto*      second = validation_block(validation_split);
    const auto window = validation_window();

    ASTIA_CHECK(heap_detail::validate(window, first, true) ==
                heap_detail::block_status::ok);
    ASTIA_CHECK(heap_detail::validate(window, second, false) ==
                heap_detail::block_status::ok);
}

ASTIA_TEST(heap_validation_rejects_out_of_window_next) {
    auto*      first  = validation_pair();
    const auto window = validation_window();

    first->next = reinterpret_cast<heap_detail::block*>(
        validation_region + sizeof(validation_region) + heap_detail::alignment);

    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::bad_next);
}

ASTIA_TEST(heap_validation_rejects_unaligned_block) {
    validation_pair();

    const auto window  = validation_window();
    auto*      shifted = reinterpret_cast<heap_detail::block*>(validation_region + 8);

    ASTIA_CHECK(heap_detail::validate(window, shifted, false) ==
                heap_detail::block_status::unaligned);
}

ASTIA_TEST(heap_validation_rejects_oversized_payload) {
    auto*      first  = validation_pair();
    const auto window = validation_window();

    first->size = sizeof(validation_region);

    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::payload_out_of_window);
}

ASTIA_TEST(heap_validation_rejects_non_reciprocal_next) {
    auto*      first  = validation_pair();
    auto*      second = validation_block(validation_split);
    const auto window = validation_window();

    second->prev = nullptr;

    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::bad_next_link);
}

ASTIA_TEST(heap_validation_rejects_non_reciprocal_prev) {
    auto*      first  = validation_pair();
    auto*      second = validation_block(validation_split);
    const auto window = validation_window();

    first->prev = second;

    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::bad_prev_link);
}

ASTIA_TEST(heap_validation_rejects_broken_adjacency) {
    auto*      first  = validation_pair();
    const auto window = validation_window();

    first->size -= heap_detail::alignment;

    ASTIA_CHECK(heap_detail::validate(window, first, true) ==
                heap_detail::block_status::bad_adjacency);
}

ASTIA_TEST(heap_validation_bounds_a_two_block_cycle) {
    auto*      first  = validation_pair();
    auto*      second = validation_block(validation_split);
    const auto window = validation_window();

    second->next = first;
    first->prev  = second;

    // Reciprocal links make both blocks pass an individual check; only the
    // walk budget turns the cycle into a failure.
    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::ok);
    ASTIA_CHECK(heap_detail::validate(window, second, false) ==
                heap_detail::block_status::ok);

    const astia::usz          budget = window.max_blocks();
    astia::usz                walked = 0;
    const heap_detail::block* cursor = first;

    while (cursor != nullptr && walked < budget) {
        cursor = cursor->next;

        ++walked;
    }

    ASTIA_CHECK(walked == budget); // the cycle would keep a bounded walk busy
}

ASTIA_TEST(heap_validation_accepts_unaligned_last_tail) {
    alignas(16) astia::u8 tail[1024 + 8]{};

    auto* only = reinterpret_cast<heap_detail::block*>(tail);

    only->size = sizeof(tail) - sizeof(heap_detail::block);
    only->prev = nullptr;
    only->next = nullptr;
    only->free = true;

    const heap_detail::window window{tail, sizeof(tail)};

    ASTIA_CHECK((only->size % heap_detail::alignment) != 0);
    ASTIA_CHECK(heap_detail::validate(window, only, false) ==
                heap_detail::block_status::ok);
}

ASTIA_TEST(heap_validation_rejects_out_of_window_prev) {
    auto*      first  = validation_pair();
    const auto window = validation_window();

    first->prev = reinterpret_cast<heap_detail::block*>(
        validation_region + sizeof(validation_region) + heap_detail::alignment);

    ASTIA_CHECK(heap_detail::validate(window, first, false) ==
                heap_detail::block_status::bad_prev);
}

ASTIA_TEST(heap_validation_rejects_partial_header) {
    validation_pair();

    const auto window = validation_window();
    auto*      tail   = reinterpret_cast<heap_detail::block*>(
        validation_region + sizeof(validation_region) - heap_detail::alignment);

    ASTIA_CHECK(heap_detail::validate(window, tail, false) ==
                heap_detail::block_status::header_out_of_window);
}

ASTIA_TEST(heap_alloc_traps_when_walk_budget_is_exhausted) {
    const pid_t child = fork();

    ASTIA_CHECK(child >= 0);

    if (child == 0) {
        // The configured heap's first block is the region itself. Turn it
        // into an allocated self-cycle so alloc() cannot take it and must
        // hit the walk budget; the host panic path traps.
        auto* head = reinterpret_cast<heap_detail::block*>(region);

        head->free = false;
        head->next = head;
        head->prev = head;

        (void) alloc(16);
        _exit(0);
    }

    int status = 0;

    ASTIA_CHECK(waitpid(child, &status, 0) == child);
    ASTIA_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
}
