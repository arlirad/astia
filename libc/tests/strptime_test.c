#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails;

#define CHECK(x)     \
    do {             \
        if (!(x))    \
            fails++; \
    } while (0)

static void test_full_timestamp(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("2026-11-23 14:05:09", "%Y-%m-%d %H:%M:%S", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126);
    CHECK(value.tm_mon == 10);
    CHECK(value.tm_mday == 23);
    CHECK(value.tm_hour == 14);
    CHECK(value.tm_min == 5);
    CHECK(value.tm_sec == 9);
    CHECK(mktime(&value) == 1795442709);
    CHECK(value.tm_wday == 1);
    CHECK(value.tm_yday == 326);
}

static void test_partial_input(void) {
    struct tm   value;
    const char* input = "2026-11-23tail";
    char*       end;

    memset(&value, 0, sizeof value);
    end = strptime(input, "%Y-%m-%d", &value);
    CHECK(end == input + 10);
    CHECK(end != NULL && strcmp(end, "tail") == 0);
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
}

static void test_single_digit_fields(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("2026-1-3", "%Y-%m-%d", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_mon == 0 && value.tm_mday == 3);
}

static void test_adjacent_fields(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("20261123", "%Y%m%d", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
}

static void test_escaped_percent(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("10%", "%H%%", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 10);
}

static void test_format_whitespace(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("2026\t  11", "%Y %m", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10);

    memset(&value, 0, sizeof value);
    end = strptime("202611", "%Y %m", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10);
}

static void test_unspecified_fields_preserved(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);
    value.tm_hour  = 7;
    value.tm_min   = 8;
    value.tm_sec   = 9;
    value.tm_wday  = 3;
    value.tm_yday  = 77;
    value.tm_isdst = 1;
    end            = strptime("2026-11-23", "%Y-%m-%d", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 7 && value.tm_min == 8 && value.tm_sec == 9);
    CHECK(value.tm_wday == 3 && value.tm_yday == 77 && value.tm_isdst == 1);
}

static void test_empty_format(void) {
    struct tm   value;
    const char* input = "unchanged";

    memset(&value, 0, sizeof value);
    CHECK(strptime(input, "", &value) == input);
}

static void test_invalid_input(void) {
    static const char* const cases[][2] = {
        {"2026-13-01", "%Y-%m-%d"},
        {"2026-00-01", "%Y-%m-%d"},
        {"2026-11-00", "%Y-%m-%d"},
        {"2026-11-32", "%Y-%m-%d"},
        {"2026-11", "%Y-%m-%d"},
        {"2026/11/23", "%Y-%m-%d"},
        {"24:00:00", "%H:%M:%S"},
        {"12:60:00", "%H:%M:%S"},
        {"12:00:61", "%H:%M:%S"},
        {"x", "%Y"},
        {"", "%Y"},
        {"2026-11-23", "%Y-%m-%dT"},
        {"2026-11-23", "%Y-%m-%d%"},
    };
    struct tm value;
    size_t    i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memset(&value, 0, sizeof value);
        CHECK(strptime(cases[i][0], cases[i][1], &value) == NULL);
    }
}

static void test_null_arguments(void) {
    struct tm value;

    memset(&value, 0, sizeof value);
    CHECK(strptime(NULL, "%Y", &value) == NULL);
    CHECK(strptime("2026", NULL, &value) == NULL);
    CHECK(strptime("2026", "%Y", NULL) == NULL);
}

static void test_strftime_round_trip(void) {
    struct tm value;
    char      text[32];
    char*     end;

    memset(&value, 0, sizeof value);
    end = strptime("2000-02-29 12:34:56", "%Y-%m-%d %H:%M:%S", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(mktime(&value) == 951827696);
    CHECK(strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &value) == 19);
    CHECK(strcmp(text, "2000-02-29 12:34:56") == 0);

    memset(&value, 0, sizeof value);
    end = strptime("1969-12-31 23:59:59", "%Y-%m-%d %H:%M:%S", &value);
    CHECK(end != NULL && *end == '\0');
    CHECK(mktime(&value) == -1);
}

int main(void) {
    test_full_timestamp();
    test_partial_input();
    test_single_digit_fields();
    test_adjacent_fields();
    test_escaped_percent();
    test_format_whitespace();
    test_unspecified_fields_preserved();
    test_empty_format();
    test_invalid_input();
    test_null_arguments();
    test_strftime_round_trip();

    if (fails)
        printf("strptime_test: %d failure(s)\n", fails);
    return fails != 0;
}
