#include "../src/proc/mmap_chunk.hpp"
#include "test.hpp"

ASTIA_TEST(mmap_chunk_covers_large_range_exactly) {
    constexpr astia::u64 pages = astia::u64{1} << 20;

    astia::u64 next    = 0;
    astia::u64 batches = 0;
    astia::u64 largest = 0;

    const astia::u64 count = astia::proc::for_each_mmap_chunk(
        0, pages, [&](const astia::proc::mmap_chunk& chunk) {
            ASTIA_CHECK(chunk.first == next);
            ASTIA_CHECK(chunk.pages != 0);
            ASTIA_CHECK(chunk.pages <= astia::proc::mmap_chunk_pages);

            if (chunk.pages > largest)
                largest = chunk.pages;

            next = chunk.end();

            ++batches;
        });

    ASTIA_CHECK(count == batches);
    ASTIA_CHECK(next == pages);
    ASTIA_CHECK(largest == astia::proc::mmap_chunk_pages);
    ASTIA_CHECK(batches == astia::proc::mmap_chunk_count(pages));
}

ASTIA_TEST(mmap_chunk_preserves_page_offset) {
    constexpr astia::u64 first_page = 0x12345;
    constexpr astia::u64 pages      = 200;

    astia::u64 next = first_page;

    astia::proc::for_each_mmap_chunk(first_page, pages,
                                     [&](const astia::proc::mmap_chunk& chunk) {
                                         ASTIA_CHECK(chunk.first == next);

                                         next = chunk.end();
                                     });

    ASTIA_CHECK(next == first_page + pages);
}

ASTIA_TEST(mmap_chunk_short_tail_stays_bounded) {
    const astia::u64 pages = astia::proc::mmap_chunk_pages * 3 + 7;

    astia::u64 batches = 0;
    astia::u64 tail    = 0;

    astia::proc::for_each_mmap_chunk(0, pages, [&](const astia::proc::mmap_chunk& chunk) {
        ++batches;

        tail = chunk.pages;
    });

    ASTIA_CHECK(batches == 4);
    ASTIA_CHECK(tail == 7);
}

ASTIA_TEST(mmap_chunk_empty_range_has_no_batches) {
    astia::u64 batches = 0;

    const astia::u64 count = astia::proc::for_each_mmap_chunk(
        42, 0, [&](const astia::proc::mmap_chunk&) { ++batches; });

    ASTIA_CHECK(count == 0);
    ASTIA_CHECK(batches == 0);
    ASTIA_CHECK(astia::proc::mmap_chunk_count(0) == 0);
}

ASTIA_TEST(mmap_chunk_count_rounds_up) {
    const astia::u64 batch = astia::proc::mmap_chunk_pages;

    ASTIA_CHECK(astia::proc::mmap_chunk_count(1) == 1);
    ASTIA_CHECK(astia::proc::mmap_chunk_count(batch) == 1);
    ASTIA_CHECK(astia::proc::mmap_chunk_count(batch + 1) == 2);
    ASTIA_CHECK(astia::proc::mmap_chunk_count(batch * 5) == 5);
}
