#include <sys/syscall.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern long syscall(long number, ...);

static int fails;

static void report(const char* text) {
    size_t length = strlen(text);

    (void) syscall(SYS_write, STDERR_FILENO, text, length);
}

static void report_line(const char* file, int line) {
    char message[160];

    (void) snprintf(message, sizeof message, "%s:%d\n", file, line);
    report(message);
}

#define CHECK(x)                             \
    do {                                     \
        if (!(x)) {                          \
            fails++;                         \
            report_line(__FILE__, __LINE__); \
        }                                    \
    } while (0)

static void expect(const char* label, const char* format, const struct tm* value,
                   const char* expected) {
    char   buffer[160] = {0};
    size_t length      = strftime(buffer, sizeof buffer, format, value);

    if (length != strlen(expected) || strcmp(buffer, expected) != 0) {
        char message[256];

        (void) snprintf(message, sizeof message,
                        "%s: got %zu \"%s\", expected %zu \"%s\"\n", label, length,
                        buffer, strlen(expected), expected);
        report(message);
        fails++;
    }
}

static void expect_epoch(const char* label, time_t epoch, const char* format,
                         const char* expected) {
    struct tm value;

    memset(&value, 0, sizeof value);

    if (gmtime_r(&epoch, &value) == NULL) {
        printf("%s: gmtime_r failed\n", label);
        fails++;

        return;
    }

    expect(label, format, &value, expected);
}

static void test_consumer_composite(void) {
    const time_t epoch = 1795442709;

    expect_epoch("prompt F T", epoch, "%F %T", "2026-11-23 14:05:09");
    expect_epoch("F", epoch, "%F", "2026-11-23");
    expect_epoch("T", epoch, "%T", "14:05:09");
    expect_epoch("R", epoch, "%R", "14:05");
    expect_epoch("r", epoch, "%r", "02:05:09 PM");
    expect_epoch("D", epoch, "%D", "11/23/26");
    expect_epoch("c", epoch, "%c", "Mon Nov 23 14:05:09 2026");
    expect_epoch("x", epoch, "%x", "11/23/26");
    expect_epoch("X", epoch, "%X", "14:05:09");
}

static void test_clock_fields(void) {
    expect_epoch("midnight p", 1795392000, "%p", "AM");
    expect_epoch("midnight P", 1795392000, "%P", "am");
    expect_epoch("midnight I", 1795392000, "%I", "12");
    expect_epoch("midnight l", 1795392000, "%l", "12");
    expect_epoch("midnight k", 1795392000, "%k", " 0");
    expect_epoch("midnight r", 1795392000, "%r", "12:00:00 AM");
    expect_epoch("midnight T", 1795392000, "%T", "00:00:00");
    expect_epoch("midnight R", 1795392000, "%R", "00:00");
    expect_epoch("noon p", 1795435200, "%p", "PM");
    expect_epoch("noon P", 1795435200, "%P", "pm");
    expect_epoch("noon I", 1795435200, "%I", "12");
    expect_epoch("noon l", 1795435200, "%l", "12");
    expect_epoch("noon k", 1795435200, "%k", "12");
    expect_epoch("noon r", 1795435200, "%r", "12:00:00 PM");
    expect_epoch("afternoon I", 1795442709, "%I", "02");
    expect_epoch("afternoon l", 1795442709, "%l", " 2");
    expect_epoch("afternoon k", 1795442709, "%k", "14");
    expect_epoch("afternoon p", 1795442709, "%p", "PM");
    expect_epoch("afternoon P", 1795442709, "%P", "pm");
    expect_epoch("early I", 1795395909, "%I", "01");
    expect_epoch("early l", 1795395909, "%l", " 1");
    expect_epoch("early k", 1795395909, "%k", " 1");
    expect_epoch("early r", 1795395909, "%r", "01:05:09 AM");
}

static void test_calendar_fields(void) {
    const time_t epoch = 1795442709;

    expect_epoch("Y", epoch, "%Y", "2026");
    expect_epoch("y", epoch, "%y", "26");
    expect_epoch("C", epoch, "%C", "20");
    expect_epoch("m", epoch, "%m", "11");
    expect_epoch("d", epoch, "%d", "23");
    expect_epoch("e", epoch, "%e", "23");
    expect_epoch("j", epoch, "%j", "327");
    expect_epoch("a", epoch, "%a", "Mon");
    expect_epoch("A", epoch, "%A", "Monday");
    expect_epoch("b", epoch, "%b", "Nov");
    expect_epoch("h", epoch, "%h", "Nov");
    expect_epoch("B", epoch, "%B", "November");
    expect_epoch("w", epoch, "%w", "1");
    expect_epoch("e day one", 1609459200, "%e", " 1");
    expect_epoch("c day one", 1609459200, "%c", "Fri Jan  1 00:00:00 2021");
    expect_epoch("C 2000", 946684800, "%C", "20");
    expect_epoch("y 2000", 946684800, "%y", "00");
    expect_epoch("F 2000", 946684800, "%F", "2000-01-01");
    expect_epoch("C 1900", -2208988800, "%C", "19");
    expect_epoch("y 1900", -2208988800, "%y", "00");
    expect_epoch("F 1900", -2208988800, "%F", "1900-01-01");
}

static void test_week_numbers(void) {
    static const struct {
        time_t      epoch;
        const char* u;
        const char* U;
        const char* W;
        const char* V;
        const char* G;
        const char* g;
    } cases[] = {
        {1795442709, "1", "47", "47", "48", "2026", "26"},
        {1709164800, "4", "08", "09", "09", "2024", "24"},
        {1609459200, "5", "00", "00", "53", "2020", "20"},
        {1609372800, "4", "52", "52", "53", "2020", "20"},
        {1735516800, "1", "52", "53", "01", "2025", "25"},
        {1767225600, "4", "00", "00", "01", "2026", "26"},
        {1798761600, "5", "00", "00", "53", "2026", "26"},
        {946684800, "6", "00", "00", "52", "1999", "99"},
        {-2208988800, "1", "00", "01", "01", "1900", "00"},
        {0, "4", "00", "00", "01", "1970", "70"},
        {-1, "3", "52", "52", "01", "1970", "70"},
    };

    size_t i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        expect_epoch("u", cases[i].epoch, "%u", cases[i].u);
        expect_epoch("U", cases[i].epoch, "%U", cases[i].U);
        expect_epoch("W", cases[i].epoch, "%W", cases[i].W);
        expect_epoch("V", cases[i].epoch, "%V", cases[i].V);
        expect_epoch("G", cases[i].epoch, "%G", cases[i].G);
        expect_epoch("g", cases[i].epoch, "%g", cases[i].g);
    }
}

static void test_epoch_seconds(void) {
    expect_epoch("s 2026", 1795442709, "%s", "1795442709");
    expect_epoch("s 1970", 0, "%s", "0");
    expect_epoch("s pre-epoch", -1, "%s", "-1");
    expect_epoch("s leap day", 1709164800, "%s", "1709164800");
    expect_epoch("s 1900", -2208988800, "%s", "-2208988800");
}

static void test_epoch_seconds_boundary(void) {
    const char* epoch_format = "%s";
    struct tm   value;

    char buffer[32];

    memset(&value, 0, sizeof value);

    value.tm_mday = 1;
    value.tm_year = INT_MAX;

    CHECK(strftime(buffer, sizeof buffer, epoch_format, &value) == 0);

    value.tm_year = INT_MAX - 1900;

    CHECK(strftime(buffer, sizeof buffer, "%Y", &value) == 10);
    CHECK(strcmp(buffer, "2147483647") == 0);
    CHECK(strftime(buffer, sizeof buffer, epoch_format, &value) == 17);
    CHECK(strcmp(buffer, "67767976201996800") == 0);
}

static void test_size_and_overflow(void) {
    struct tm   value;
    struct tm   original;
    const char* null_format  = NULL;
    const char* empty_format = "";
    const char* epoch_format = "%s";

    char buffer[32];

    memset(&value, 0, sizeof value);
    CHECK(gmtime_r(&(time_t){1795442709}, &value) != NULL);

    original = value;

    CHECK(strftime(buffer, 11, "%F", &value) == 10);
    CHECK(strcmp(buffer, "2026-11-23") == 0);
    CHECK(strftime(buffer, 10, "%F", &value) == 0);
    CHECK(strftime(buffer, 0, "%F", &value) == 0);
    CHECK(strftime(NULL, 10, "%F", &value) == 0);
    CHECK(strftime(buffer, sizeof buffer, null_format, &value) == 0);
    CHECK(strftime(buffer, sizeof buffer, "%F", NULL) == 0);

    CHECK(strftime(buffer, 20, "%F %T", &value) == 19);
    CHECK(strcmp(buffer, "2026-11-23 14:05:09") == 0);
    CHECK(strftime(buffer, 19, "%F %T", &value) == 0);

    CHECK(strftime(buffer, 2, "A", &value) == 1);
    CHECK(strcmp(buffer, "A") == 0);
    CHECK(strftime(buffer, 1, "A", &value) == 0);

    memset(buffer, 'X', sizeof buffer);
    CHECK(strftime(buffer, sizeof buffer, empty_format, &value) == 0);
    CHECK(buffer[0] == '\0');

    CHECK(strftime(buffer, 11, epoch_format, &value) == 10);
    CHECK(strcmp(buffer, "1795442709") == 0);
    CHECK(strftime(buffer, 10, epoch_format, &value) == 0);

    CHECK(memcmp(&original, &value, sizeof value) == 0);
}

static void test_unsupported(void) {
    static const char* const formats[] = {
        "%Z", "%z", "%N", "%v", "%Q", "%Ec", "%Od", "%-d", "%_d", "%0e", "%#a",
    };
    struct tm value;

    char   buffer[32];
    size_t i;

    memset(&value, 0, sizeof value);
    CHECK(gmtime_r(&(time_t){1795442709}, &value) != NULL);

    for (i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        CHECK(strftime(buffer, sizeof buffer, formats[i], &value) == 0);
    }

    CHECK(strftime(buffer, sizeof buffer, "ok%Z", &value) == 0);
    CHECK(strftime(buffer, sizeof buffer, "ok%z", &value) == 0);
}

static void test_preserved_directives(void) {
    const time_t epoch = 1795442709;

    expect_epoch("preserved", epoch, "%a|%A|%b|%h|%B|%Y|%m|%d|%H|%M|%S|%j|%w|%%",
                 "Mon|Monday|Nov|Nov|November|2026|11|23|14|05|09|327|1|%");
    expect_epoch("newline", epoch, "%n", "\n");
    expect_epoch("tab", epoch, "%t", "\t");
}

int main(void) {
    test_consumer_composite();
    test_clock_fields();
    test_calendar_fields();
    test_week_numbers();
    test_epoch_seconds();
    test_epoch_seconds_boundary();
    test_size_and_overflow();
    test_unsupported();
    test_preserved_directives();

    if (fails) {
        char message[64];

        (void) snprintf(message, sizeof message, "strftime_test: %d failure(s)\n", fails);
        report(message);
    }

    return fails != 0;
}
