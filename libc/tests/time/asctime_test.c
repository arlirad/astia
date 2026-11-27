#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;

#define CHECK(x)     \
    do {             \
        if (!(x))    \
            fails++; \
    } while (0)

static void expect_asctime(const char* expected, time_t epoch) {
    struct tm value;
    char*     text;

    CHECK(gmtime_r(&epoch, &value) != NULL);

    text = asctime(&value);

    if (text == NULL || strcmp(text, expected) != 0) {
        printf("asctime(%ld): got \"%s\", expected \"%s\"\n", (long) epoch,
               text == NULL ? "(null)" : text, expected);
        fails++;

        return;
    }

    CHECK(strlen(text) == 25);
    CHECK(text[24] == '\n');
}

static void expect_ctime(const char* expected, time_t epoch) {
    const char* text = ctime(&epoch);

    if (text == NULL || strcmp(text, expected) != 0) {
        printf("ctime(%ld): got \"%s\", expected \"%s\"\n", (long) epoch,
               text == NULL ? "(null)" : text, expected);
        fails++;

        return;
    }

    CHECK(strlen(text) == 25);
    CHECK(text[24] == '\n');
}

static void test_single_digit_day(void) {
    expect_asctime("Thu Jan  1 00:00:00 1970\n", 0);
    expect_asctime("Fri Jan  9 00:00:00 1970\n", 691200);
}

static void test_two_digit_day(void) {
    expect_asctime("Sat Jan 10 00:00:00 1970\n", 777600);
    expect_asctime("Sat Jan 31 00:00:00 1970\n", 2592000);
}

static void test_ctime_delegation(void) {
    expect_ctime("Thu Jan  1 00:00:00 1970\n", 0);
    expect_ctime("Fri Jan  9 00:00:00 1970\n", 691200);
    expect_ctime("Sat Jan 10 00:00:00 1970\n", 777600);
}

static void test_null_argument(void) {
    CHECK(asctime(NULL) == NULL);
}

int main(void) {
    test_single_digit_day();
    test_two_digit_day();
    test_ctime_delegation();
    test_null_argument();

    if (fails)
        printf("asctime_test: %d failure(s)\n", fails);

    return fails != 0;
}
