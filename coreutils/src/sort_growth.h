#pragma once

#include <stddef.h>

#define CU_SORT_INITIAL_CAPACITY 16

typedef void* (*cu_sort_grow_fn)(void* block, size_t bytes);

typedef struct {
    size_t capacity;
    size_t bytes;
} cu_sort_growth;

static inline int cu_sort_plan_growth(size_t capacity, cu_sort_growth* growth) {
    size_t next = CU_SORT_INITIAL_CAPACITY;

    if (capacity != 0) {
        if (capacity > ((size_t) -1) / 2)
            return -1;

        next = capacity * 2;
    }

    if (next > ((size_t) -1) / sizeof(char*))
        return -1;

    growth->capacity = next;
    growth->bytes    = next * sizeof(char*);

    return 0;
}

static inline int cu_sort_grow_lines(char*** lines, size_t* capacity,
                                     cu_sort_grow_fn grow) {
    cu_sort_growth growth;

    if (cu_sort_plan_growth(*capacity, &growth) != 0)
        return -1;

    char** grown = (char**) grow(*lines, growth.bytes);
    if (grown == 0)
        return -1;

    *lines    = grown;
    *capacity = growth.capacity;

    return 0;
}
