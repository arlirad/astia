#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void __astia_assert_fail(const char* expr, const char* file, int line, const char* func) {
    printf("%s:%d: assertion failed: %s (in %s)\n", file, line, expr, func);
    abort();
}
