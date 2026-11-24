#include "../src/ls_layout.h"

#include <stdio.h>

static int failures;

static void expect(const char* what, size_t actual, size_t expected) {
    if (actual == expected)
        return;
    printf("ls layout test failed: %s: got %lu expected %lu\n", what,
           (unsigned long) actual, (unsigned long) expected);
    ++failures;
}

int main(void) {
    expect("columns 16/5", cu_ls_column_count(5, 16), 2);
    expect("columns 40/5", cu_ls_column_count(5, 40), 6);
    expect("columns 80/1", cu_ls_column_count(1, 80), 27);
    expect("columns too narrow", cu_ls_column_count(30, 20), 1);
    expect("columns exact", cu_ls_column_count(5, 5), 1);
    expect("columns empty", cu_ls_column_count(0, 80), 27);
    expect("rows four", cu_ls_row_count(4, 2), 2);
    expect("rows five", cu_ls_row_count(5, 2), 3);
    expect("rows empty", cu_ls_row_count(0, 2), 0);
    expect("rows no columns", cu_ls_row_count(3, 0), 0);

    const size_t widths[] = {5, 4, 5, 5};
    expect("width column 0", cu_ls_column_width(widths, 4, 2, 0), 5);
    expect("width column 1", cu_ls_column_width(widths, 4, 2, 1), 5);
    const size_t uneven[] = {1, 7, 2, 9, 3};
    expect("width uneven 0", cu_ls_column_width(uneven, 5, 2, 0), 7);
    expect("width uneven 1", cu_ls_column_width(uneven, 5, 2, 1), 9);
    expect("width uneven 2", cu_ls_column_width(uneven, 5, 2, 2), 3);
    expect("width partial", cu_ls_column_width(uneven, 3, 2, 1), 2);
    expect("width empty", cu_ls_column_width(widths, 0, 0, 0), 0);

    expect("color tty", (size_t) cu_ls_color_enabled(1, 0), 1);
    expect("color no_color", (size_t) cu_ls_color_enabled(1, 1), 0);
    expect("color pipe", (size_t) cu_ls_color_enabled(0, 0), 0);

    if (failures != 0)
        return 1;
    printf("ls layout tests ok\n");
    return 0;
}
