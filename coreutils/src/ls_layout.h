#pragma once

#include <stddef.h>

#define CU_LS_COLUMN_GAP 2

static inline size_t cu_ls_column_count(size_t longest, size_t terminal_width) {
    if (longest == 0)
        longest = 1;
    if (terminal_width <= longest)
        return 1;
    return (terminal_width + CU_LS_COLUMN_GAP) / (longest + CU_LS_COLUMN_GAP);
}

static inline size_t cu_ls_row_count(size_t count, size_t columns) {
    if (columns == 0)
        return 0;
    return (count + columns - 1) / columns;
}

static inline size_t cu_ls_column_width(const size_t* widths, size_t count, size_t rows,
                                        size_t column) {
    size_t result = 0;
    size_t start  = column * rows;
    size_t end    = start + rows;
    if (end > count)
        end = count;
    for (size_t index = start; index < end; ++index)
        if (widths[index] > result)
            result = widths[index];
    return result;
}

static inline int cu_ls_color_enabled(int is_tty, int no_color_present) {
    return is_tty && !no_color_present;
}
