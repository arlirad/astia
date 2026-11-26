#include <limits.h>
#include <setjmp.h>
#include <stdio.h>

static jmp_buf environment;
static int     failures;
static int     first_result;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static int jump_with_value(int value) {
    volatile int stage  = 0;
    int          result = setjmp(environment);

    if (stage == 0) {
        stage        = 1;
        first_result = result;

        longjmp(environment, value);
    }

    return result;
}

int main(void) {
    CHECK(jump_with_value(42) == 42);
    CHECK(first_result == 0);
    CHECK(jump_with_value(1) == 1);
    CHECK(jump_with_value(0) == 1);
    CHECK(jump_with_value(-42) == -42);
    CHECK(jump_with_value(INT_MAX) == INT_MAX);

    if (failures != 0)
        printf("setjmp_test: %d failure(s)\n", failures);

    return failures != 0;
}
