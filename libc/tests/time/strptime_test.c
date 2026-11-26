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

    size_t i;

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

    char text[32];

    char* end;

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

static void test_two_digit_year(void) {
    static const struct {
        const char* text;
        int         year;
    } cases[] = {
        {"00", 100}, {"24", 124}, {"68", 168}, {"69", 69}, {"99", 99},
    };

    struct tm value;

    size_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char* end;

        memset(&value, 0, sizeof value);

        end = strptime(cases[i].text, "%y", &value);

        CHECK(end != NULL && *end == '\0');
        CHECK(value.tm_year == cases[i].year);
    }
}

static void test_month_weekday_names(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime("Monday November", "%A %B", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_wday == 1 && value.tm_mon == 10);

    memset(&value, 0, sizeof value);

    end = strptime("monday november", "%A %B", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_wday == 1 && value.tm_mon == 10);

    memset(&value, 0, sizeof value);

    end = strptime("Wed Nov", "%a %h", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_wday == 3 && value.tm_mon == 10);

    memset(&value, 0, sizeof value);
    CHECK(strptime("Funday", "%A", &value) == NULL);
    CHECK(strptime("Nox 1", "%b %d", &value) == NULL);
}

static void test_rfc1123(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime("Mon, 23 Nov 2026 14:05:09 +0000", "%a, %d %b %Y %H:%M:%S %z", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);
    CHECK(value.tm_wday == 1);

    memset(&value, 0, sizeof value);

    end = strptime("monday, 23 november 2026 14:05:09 -05:30", "%A, %d %B %Y %H:%M:%S %z",
                   &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);
}

static void test_padded_and_clock_fields(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime(" 1  9", "%e %k", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_mday == 1 && value.tm_hour == 9);

    memset(&value, 0, sizeof value);

    end = strptime(" 2", "%l", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 2);

    memset(&value, 0, sizeof value);

    end = strptime("02:05:09 PM", "%r", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);

    memset(&value, 0, sizeof value);

    end = strptime("12:00:00 AM", "%r", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 0);

    memset(&value, 0, sizeof value);

    end = strptime("12:00:00 PM", "%r", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 12);

    memset(&value, 0, sizeof value);

    end = strptime("11 pm", "%I %p", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 23);

    memset(&value, 0, sizeof value);

    end = strptime("11 am", "%I %P", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 11);

    memset(&value, 0, sizeof value);
    CHECK(strptime("11 xm", "%I %p", &value) == NULL);
}

static void test_day_of_year_and_week_fields(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime("327", "%j", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_yday == 326);

    memset(&value, 0, sizeof value);

    end = strptime("1", "%u", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_wday == 1);

    memset(&value, 0, sizeof value);

    end = strptime("0", "%w", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_wday == 0);

    memset(&value, 0, sizeof value);

    end = strptime("2026 47 1", "%Y %U %w", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_wday == 1 && value.tm_yday == 326);

    memset(&value, 0, sizeof value);

    end = strptime("2026 47 1", "%Y %W %u", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_wday == 1 && value.tm_yday == 326);

    memset(&value, 0, sizeof value);

    end = strptime("2020 53 5", "%G %V %u", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 120 && value.tm_wday == 5 && value.tm_yday == 366);

    memset(&value, 0, sizeof value);

    end = strptime("26 48 1", "%g %V %u", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_wday == 1 && value.tm_yday == 326);

    memset(&value, 0, sizeof value);
    CHECK(strptime("367", "%j", &value) == NULL);
    CHECK(strptime("2026 54 1", "%Y %U %w", &value) == NULL);
    CHECK(strptime("00", "%V", &value) == NULL);
}

static void test_century(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime("20 26", "%C %y", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126);

    memset(&value, 0, sizeof value);

    end = strptime("19 99", "%C %y", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 99);

    memset(&value, 0, sizeof value);

    end = strptime("20", "%C", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 100);
}

static void test_zone_and_epoch(void) {
    const time_t epoch = 1795442709;
    struct tm    value;
    char*        end;

    memset(&value, 0, sizeof value);

    end = strptime("UTC", "%Z", &value);

    CHECK(end != NULL && *end == '\0');

    memset(&value, 0, sizeof value);

    end = strptime("+0530", "%z", &value);

    CHECK(end != NULL && *end == '\0');

    memset(&value, 0, sizeof value);

    end = strptime("-05:30", "%z", &value);

    CHECK(end != NULL && *end == '\0');

    CHECK(strptime("UTC", "%z", &value) == NULL);
    CHECK(strptime("123", "%Z", &value) == NULL);

    memset(&value, 0, sizeof value);

    end = strptime("1795442709", "%s", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);
    CHECK(value.tm_wday == 1 && value.tm_yday == 326);
    CHECK(mktime(&value) == epoch);
}

static void test_composite_conversions(void) {
    struct tm value;
    char*     end;

    memset(&value, 0, sizeof value);

    end = strptime("Mon Nov 23 14:05:09 2026", "%c", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);

    memset(&value, 0, sizeof value);

    end = strptime("11/23/26", "%D", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);

    memset(&value, 0, sizeof value);

    end = strptime("2026-11-23", "%F", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);

    memset(&value, 0, sizeof value);

    end = strptime("14:05", "%R", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 14 && value.tm_min == 5);

    memset(&value, 0, sizeof value);

    end = strptime("14:05:09", "%T", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);

    memset(&value, 0, sizeof value);

    end = strptime("11/23/26", "%x", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_year == 126 && value.tm_mon == 10 && value.tm_mday == 23);

    memset(&value, 0, sizeof value);

    end = strptime("14:05:09", "%X", &value);

    CHECK(end != NULL && *end == '\0');
    CHECK(value.tm_hour == 14 && value.tm_min == 5 && value.tm_sec == 9);
}

static void test_extended_round_trip(void) {
    const time_t epoch = 1795442709;
    struct tm    source;
    struct tm    parsed;

    char text[128];
    char again[128];

    char* end;

    CHECK(gmtime_r(&epoch, &source) != NULL);

    CHECK(strftime(text, sizeof text, "%a, %d %b %Y %H:%M:%S", &source) > 0);

    memset(&parsed, 0, sizeof parsed);

    end = strptime(text, "%a, %d %b %Y %H:%M:%S", &parsed);

    CHECK(end != NULL && *end == '\0');
    CHECK(mktime(&parsed) == epoch);

    CHECK(strftime(text, sizeof text, "%C %y %j %U %W %w", &source) > 0);

    memset(&parsed, 0, sizeof parsed);

    end = strptime(text, "%C %y %j %U %W %w", &parsed);

    CHECK(end != NULL && *end == '\0');
    CHECK(parsed.tm_year == source.tm_year && parsed.tm_yday == source.tm_yday);
    CHECK(strftime(again, sizeof again, "%U %W %w", &parsed) > 0);
    CHECK(strcmp(again, "47 47 1") == 0);

    CHECK(strftime(text, sizeof text, "%G %V %u", &source) > 0);

    memset(&parsed, 0, sizeof parsed);

    end = strptime(text, "%G %V %u", &parsed);

    CHECK(end != NULL && *end == '\0');
    CHECK(strftime(again, sizeof again, "%G %V %u", &parsed) > 0);
    CHECK(strcmp(again, text) == 0);
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
    test_two_digit_year();
    test_month_weekday_names();
    test_rfc1123();
    test_padded_and_clock_fields();
    test_day_of_year_and_week_fields();
    test_century();
    test_zone_and_epoch();
    test_composite_conversions();
    test_extended_round_trip();

    if (fails)
        printf("strptime_test: %d failure(s)\n", fails);

    return fails != 0;
}
