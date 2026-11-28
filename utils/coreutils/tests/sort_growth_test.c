#include "../src/sort_growth.h"

#include <stdio.h>
#include <stdlib.h>

static int          failures;
static size_t       grow_calls;
static size_t       grow_bytes;
static void*        grow_result;
static int          grow_fail;
static cu_sort_line mock_block[4];

static void* mock_grow(void* block, size_t bytes) {
    (void) block;

    ++grow_calls;

    grow_bytes = bytes;

    return grow_fail ? NULL : grow_result;
}

static void expect(const char* what, size_t actual, size_t expected) {
    if (actual == expected)
        return;

    printf("sort growth test failed: %s: got %lu expected %lu\n", what,
           (unsigned long) actual, (unsigned long) expected);
    ++failures;
}

static void expect_plan(const char* what, size_t capacity, int status,
                        size_t expected_capacity) {
    cu_sort_growth growth;

    expect(what, (size_t) cu_sort_plan_growth(capacity, &growth), (size_t) status);

    if (status != 0)
        return;

    expect(what, growth.capacity, expected_capacity);
    expect(what, growth.bytes, expected_capacity * sizeof(cu_sort_line));
}

static void plan_checks(void) {
    const size_t maximum = (size_t) -1;

    expect_plan("initial", 0, 0, CU_SORT_INITIAL_CAPACITY);
    expect_plan("small", 1, 0, 2);
    expect_plan("odd", 3, 0, 6);
    expect_plan("full block", CU_SORT_INITIAL_CAPACITY, 0, 2 * CU_SORT_INITIAL_CAPACITY);
    expect_plan("bytes boundary", maximum / (2 * sizeof(cu_sort_line)), 0,
                2 * (maximum / (2 * sizeof(cu_sort_line))));
    expect_plan("bytes overflow", maximum / (2 * sizeof(cu_sort_line)) + 1, -1, 0);
    expect_plan("capacity overflow", maximum / 2 + 1, -1, 0);
    expect_plan("capacity maximum", maximum, -1, 0);
}

static void grow_checks(void) {
    const size_t maximum = (size_t) -1;

    cu_sort_line  sentinel;
    cu_sort_line* lines = &sentinel;

    size_t capacity = 0;

    grow_calls  = 0;
    grow_result = mock_block;
    grow_fail   = 0;

    expect("grow status", (size_t) cu_sort_grow_lines(&lines, &capacity, mock_grow), 0);
    expect("grow calls", grow_calls, 1);
    expect("grow bytes", grow_bytes, CU_SORT_INITIAL_CAPACITY * sizeof(cu_sort_line));
    expect("grow capacity", capacity, CU_SORT_INITIAL_CAPACITY);
    expect("grow pointer", (size_t) lines, (size_t) mock_block);

    grow_result = NULL;
    grow_fail   = 1;
    lines       = &sentinel;
    capacity    = 2;

    expect("rejected status", (size_t) cu_sort_grow_lines(&lines, &capacity, mock_grow),
           (size_t) -1);
    expect("rejected capacity", capacity, 2);
    expect("rejected pointer", (size_t) lines, (size_t) &sentinel);

    grow_fail = 0;
    capacity  = maximum / 2 + 1;

    expect("overflow status", (size_t) cu_sort_grow_lines(&lines, &capacity, mock_grow),
           (size_t) -1);
    expect("overflow capacity", capacity, maximum / 2 + 1);
}

static void real_allocation_checks(void) {
    cu_sort_line* lines;

    size_t capacity;

    lines    = NULL;
    capacity = 0;

    for (size_t step = 0; step < 5; ++step) {
        if (cu_sort_grow_lines(&lines, &capacity, realloc) != 0) {
            printf("sort growth test failed: real growth step %lu\n",
                   (unsigned long) step);
            ++failures;

            break;
        }

        expect("real growth capacity", capacity, CU_SORT_INITIAL_CAPACITY << step);
    }

    free(lines);
}

int main(void) {
    plan_checks();
    grow_checks();
    real_allocation_checks();

    return failures == 0 ? 0 : 1;
}
