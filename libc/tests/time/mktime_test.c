#include <sys/syscall.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern long syscall(long number, ...);

static int fails;

#define CHECK(x)     \
    do {             \
        if (!(x))    \
            fails++; \
    } while (0)

static void report(const char* text) {
    (void) syscall(SYS_write, STDERR_FILENO, text, strlen(text));
}

static struct tm utc_tm(int64_t year, int month, int mday, int hour, int minute,
                        int second) {
    struct tm value = {0};

    value.tm_year = (int) (year - 1900);
    value.tm_mon  = month;
    value.tm_mday = mday;
    value.tm_hour = hour;
    value.tm_min  = minute;
    value.tm_sec  = second;

    return value;
}

static void expect_epoch(const char* label, struct tm* value, time_t expected) {
    char message[160];

    time_t result = mktime(value);

    if (result != expected) {
        (void) snprintf(message, sizeof message,
                        "%s: mktime returned %ld, expected %ld\n", label, (long) result,
                        (long) expected);
        report(message);
        fails++;
    }
}

static void expect_fields(const char* label, const struct tm* value, int64_t year,
                          int month, int mday, int hour, int minute, int second) {
    char message[192];

    if (value->tm_year != (int) (year - 1900) || value->tm_mon != month ||
        value->tm_mday != mday || value->tm_hour != hour || value->tm_min != minute ||
        value->tm_sec != second) {
        (void) snprintf(message, sizeof message,
                        "%s: normalized %d-%02d-%02d %02d:%02d:%02d, expected "
                        "%04d-%02d-%02d %02d:%02d:%02d\n",
                        label, value->tm_year + 1900, value->tm_mon + 1, value->tm_mday,
                        value->tm_hour, value->tm_min, value->tm_sec, (int) year,
                        month + 1, mday, hour, minute, second);
        report(message);
        fails++;
    }
}

static void expect_overflow(const char* label, struct tm value) {
    char message[160];
    int  original_year = value.tm_year;

    time_t result;

    errno  = 0;
    result = mktime(&value);

    if (result != (time_t) -1 || errno != EOVERFLOW) {
        (void) snprintf(message, sizeof message,
                        "%s: mktime returned %ld errno %d, expected -1/EOVERFLOW\n",
                        label, (long) result, errno);
        report(message);
        fails++;
    }

    CHECK(value.tm_year == original_year);
}

static void test_epoch(void) {
    struct tm value = utc_tm(1970, 0, 1, 0, 0, 0);

    expect_epoch("epoch", &value, 0);
    expect_fields("epoch", &value, 1970, 0, 1, 0, 0, 0);
}

static void test_far_years(void) {
    struct tm value = utc_tm(2500, 0, 1, 0, 0, 0);

    expect_epoch("2500-01-01", &value, 16725225600L);
    expect_fields("2500-01-01", &value, 2500, 0, 1, 0, 0, 0);

    value = utc_tm(2500, 5, 15, 12, 34, 56);

    expect_epoch("2500-06-15", &value, 16739526896L);

    value = utc_tm(9999, 11, 31, 23, 59, 59);

    expect_epoch("9999-12-31", &value, 253402300799L);
    expect_fields("9999-12-31", &value, 9999, 11, 31, 23, 59, 59);

    value = utc_tm(9999, 0, 1, 0, 0, 0);

    expect_epoch("9999-01-01", &value, 253370764800L);
}

static void test_month_normalization(void) {
    struct tm value = {.tm_year = 99, .tm_mon = 13, .tm_mday = 1};

    expect_epoch("month 13", &value, 949363200L);
    expect_fields("month 13", &value, 2000, 1, 1, 0, 0, 0);

    value = utc_tm(2000, -1, 1, 0, 0, 0);

    expect_epoch("month -1", &value, 944006400L);
    expect_fields("month -1", &value, 1999, 11, 1, 0, 0, 0);

    value = utc_tm(2024, 0, 0, 0, 0, 0);

    expect_epoch("mday 0", &value, 1703980800L);
    expect_fields("mday 0", &value, 2023, 11, 31, 0, 0, 0);
}

static void test_large_mday(void) {
    struct tm value = utc_tm(2024, 0, 100000, 0, 0, 0);

    expect_epoch("mday 100000", &value, 10343980800L);
    expect_fields("mday 100000", &value, 2297, 9, 15, 0, 0, 0);

    value = utc_tm(2024, 0, -99, 0, 0, 0);

    expect_epoch("mday -99", &value, 1695427200L);
    expect_fields("mday -99", &value, 2023, 8, 23, 0, 0, 0);

    value = utc_tm(2024, 0, INT_MAX, 0, 0, 0);

    expect_epoch("mday INT_MAX", &value, 185544291081600L);
    expect_fields("mday INT_MAX", &value, 5881634, 6, 10, 0, 0, 0);
}

static void test_negative_fields(void) {
    struct tm value = utc_tm(1969, 11, 31, 23, 59, 59);

    errno = 0;

    CHECK(mktime(&value) == (time_t) -1);
    CHECK(errno == 0);

    value = utc_tm(1970, 0, 1, -1, 0, 0);

    expect_epoch("hour -1", &value, -3600L);
    expect_fields("hour -1", &value, 1969, 11, 31, 23, 0, 0);

    value = utc_tm(1970, 0, 1, 0, -1, 0);

    expect_epoch("minute -1", &value, -60L);
    expect_fields("minute -1", &value, 1969, 11, 31, 23, 59, 0);

    value = utc_tm(1970, 0, 1, 0, 0, -1);

    expect_epoch("second -1", &value, -1L);
    expect_fields("second -1", &value, 1969, 11, 31, 23, 59, 59);
}

static void test_overflow(void) {
    struct tm value = {0};

    value.tm_year = INT_MAX;

    expect_overflow("tm_year INT_MAX", value);

    value.tm_mon = 1;

    expect_overflow("tm_year INT_MAX month 1", value);

    value = utc_tm(100, INT_MAX, 1, 0, 0, 0);

    expect_epoch("month INT_MAX", &value, 5647277519193600L);
    expect_fields("month INT_MAX", &value, 178957070, 7, 1, 0, 0, 0);

    value = utc_tm(100, INT_MIN, 1, 0, 0, 0);

    expect_epoch("month INT_MIN", &value, -5647395545049600L);
    expect_fields("month INT_MIN", &value, -178956871, 4, 1, 0, 0, 0);

    value         = (struct tm){0};
    value.tm_year = INT_MIN;
    value.tm_mday = INT_MIN;

    expect_overflow("mday INT_MIN", value);
}

static void test_round_trip_far_date(void) {
    struct tm value = utc_tm(2500, 5, 15, 12, 34, 56);
    struct tm back;

    time_t result = mktime(&value);

    CHECK(result == 16739526896L);
    CHECK(localtime_r(&result, &back) == &back);
    expect_fields("round trip", &back, 2500, 5, 15, 12, 34, 56);
    CHECK(back.tm_wday == value.tm_wday);
    CHECK(back.tm_yday == value.tm_yday);
}

static void test_gmtime_overflow(void) {
    struct tm value;

    time_t maximum = INT64_MAX;
    time_t minimum = INT64_MIN;

    errno = 0;

    CHECK(gmtime_r(&maximum, &value) == NULL);
    CHECK(errno == EOVERFLOW);

    errno = 0;

    CHECK(gmtime_r(&minimum, &value) == NULL);
    CHECK(errno == EOVERFLOW);
}

static void test_null_argument(void) {
    errno = 0;

    CHECK(mktime(NULL) == (time_t) -1);
    CHECK(errno == EINVAL);
}

int main(void) {
    test_epoch();
    test_far_years();
    test_month_normalization();
    test_large_mday();
    test_negative_fields();
    test_overflow();
    test_round_trip_far_date();
    test_gmtime_overflow();
    test_null_argument();

    if (fails) {
        char message[64];

        (void) snprintf(message, sizeof message, "mktime_test: %d failure(s)\n", fails);
        report(message);
    }

    return fails != 0;
}
