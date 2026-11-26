#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* <unistd.h> is ours now (getpid/gettid); the one host call this test
 * needs is declared by hand, like every other host port here. */
extern int unlink(const char* path);
#include <sys/time.h>

#include <time.h>
#include <wchar.h>
#include <wctype.h>

extern int __astia_time_host_clock_id;

static int fails;
static int exit_order;

#define CHECK(x)     \
    do {             \
        if (!(x))    \
            fails++; \
    } while (0)

static void exit_first(void) {
    exit_order = exit_order * 10 + 1;
}

static void exit_second(void) {
    exit_order = exit_order * 10 + 2;
}

static void test_stream_helpers(void) {
    char buf[8];

    FILE*  f = fopen("/tmp/astia_conformance_test.dat", "w+");
    fpos_t pos;

    CHECK(f != NULL);

    if (f == NULL)
        return;

    CHECK(putc('a', f) == 'a');
    CHECK(putc('b', f) == 'b');
    CHECK(fflush(f) == 0);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(getc(f) == 'a');
    CHECK(ungetc('x', f) == 'x');
    CHECK(getc(f) == 'x');
    CHECK(fgetpos(f, &pos) == 0);
    CHECK(getc(f) == 'b');
    CHECK(fsetpos(f, &pos) == 0);
    CHECK(getc(f) == 'b');
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fread(buf, 1, 2, f) == 2);
    CHECK(memcmp(buf, "ab", 2) == 0);
    CHECK(fclose(f) == 0);
    (void) unlink("/tmp/astia_conformance_test.dat");
}

static void test_conversions_and_strings(void) {
    char* end;
    char* copy;

    char               transformed[8];
    long long          signed_value;
    unsigned long long unsigned_value;

    signed_value = strtoll("-9223372036854775808x", &end, 10);

    CHECK(signed_value == LLONG_MIN && *end == 'x');

    unsigned_value = strtoull("18446744073709551615x", &end, 10);

    CHECK(unsigned_value == ULLONG_MAX && *end == 'x');

    copy = strdup("Hello");

    CHECK(copy != NULL && strcmp(copy, "Hello") == 0);
    free(copy);

    copy = strndup("abcdef", 3);

    CHECK(copy != NULL && strcmp(copy, "abc") == 0);
    free(copy);
    CHECK(strcasecmp("AbC", "aBc") == 0);
    CHECK(strncasecmp("abcd", "ABce", 3) == 0);
    CHECK(strcoll("a", "b") < 0);
    CHECK(strxfrm(transformed, "abcdef", sizeof transformed) == 6);
    CHECK(strcmp(transformed, "abcdef") == 0);
}

static void test_printf_family(void) {
    char buf[32];

    char* end;

    double value;

    CHECK(snprintf(buf, sizeof buf, "%s %d", "ok", 42) == 5);
    CHECK(strcmp(buf, "ok 42") == 0);
    CHECK(sprintf(buf, "%x", 255U) == 2);
    CHECK(strcmp(buf, "ff") == 0);
    CHECK(snprintf(buf, sizeof buf, "%.2f", 1.25) == 4);
    CHECK(strcmp(buf, "1.25") == 0);
    CHECK(snprintf(buf, sizeof buf, "%.2e", 12.5) == 8);
    CHECK(strcmp(buf, "1.25e+01") == 0);
    CHECK(snprintf(buf, sizeof buf, "%.4g", 12.5) == 4);
    CHECK(strcmp(buf, "12.5") == 0);
    CHECK(snprintf(buf, sizeof buf, "%a", 1.5) == 8);
    CHECK(strcmp(buf, "0x1.8p+0") == 0);
    CHECK(strtod("0x1.8p+1", &end) == 3.0 && *end == '\0');

    value = strtod("-3.125e2x", &end);

    CHECK(value == -312.5 && *end == 'x');
}

static void test_scan_family(void) {
    char     word[8];
    char     set[8];
    int      a;
    unsigned b;
    double   value;
    int      count;

    CHECK(sscanf("-12 0xff word", "%d %x %7s", &a, &b, word) == 3);
    CHECK(a == -12 && b == 255 && strcmp(word, "word") == 0);
    CHECK(sscanf("abc123", "%[a-z]%n", set, &count) == 1);
    CHECK(strcmp(set, "abc") == 0 && count == 3);
    CHECK(sscanf("3.125", "%lf", &value) == 1);
    CHECK(value == 3.125);
    CHECK(sscanf("xy", "%2c", set) == 1);
    CHECK(set[0] == 'x' && set[1] == 'y');
}

static void test_c_locale(void) {
    struct lconv* lc = localeconv();

    CHECK(strcmp(setlocale(LC_ALL, NULL), "C") == 0);
    CHECK(strcmp(setlocale(LC_NUMERIC, "C"), "C") == 0);
    CHECK(setlocale(LC_ALL, "en_US.UTF-8") == NULL);
    CHECK(strcmp(lc->decimal_point, ".") == 0);
    CHECK(strcmp(lc->thousands_sep, "") == 0);
}

static void test_process_hooks(void) {
    exit_order = 0;

    CHECK(atexit(exit_first) == 0);
    CHECK(atexit(exit_second) == 0);
    /* Registration is observable without terminating this test process. */
    CHECK(exit_order == 0);
}

static void test_wide_c_locale(void) {
    wchar_t wide[16];
    wchar_t back[16];

    const wchar_t* source = L"alpha beta";
    const char*    text   = "alpha beta";

    CHECK(wcslen(source) == 10);
    CHECK(wcscmp(wcschr(source, L'b'), L"beta") == 0);
    CHECK(wcscpy(wide, source) == wide);
    CHECK(wcscat(wide, L"!") == wide);
    CHECK(wcscmp(wide, L"alpha beta!") == 0);
    CHECK(wcstombs(NULL, source, 0) == 10);
    CHECK(mbstowcs(back, text, 16) == 10);
    CHECK(wcscmp(back, source) == 0);
    CHECK(iswalpha(L'A') && iswdigit(L'7') && !iswalpha(0x100));
    CHECK(towlower(L'A') == L'a' && towupper(L'z') == L'Z');
    CHECK(wctob(L'A') == 'A' && btowc('A') == L'A');
}

static void test_utc_time(void) {
    time_t    epoch = 0;
    struct tm value;

    char text[32];

    struct timespec now;

    CHECK(gmtime_r(&epoch, &value) == &value);
    CHECK(value.tm_year == 70 && value.tm_mon == 0 && value.tm_mday == 1);
    CHECK(value.tm_hour == 0 && value.tm_wday == 4 && value.tm_yday == 0);
    CHECK(strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &value) == 19);
    CHECK(strcmp(text, "1970-01-01 00:00:00") == 0);
    CHECK(mktime(&value) == 0);
    CHECK(clock_gettime(CLOCK_REALTIME, &now) == 0 && now.tv_sec > 0);
    CHECK(clock_getres(CLOCK_REALTIME, &now) == 0 && now.tv_sec == 0 && now.tv_nsec > 0 &&
          now.tv_nsec < 1000000000L);
    CHECK(clock_getres(CLOCK_MONOTONIC, &now) == 0 && now.tv_sec == 0 &&
          now.tv_nsec > 0 && now.tv_nsec < 1000000000L);

    errno = 0;

    CHECK(clock_getres(CLOCK_BOOTTIME, &now) == -1 && errno == EINVAL);
    CHECK(clock_getres(CLOCK_REALTIME, NULL) == 0);
    CHECK(timespec_get(&now, TIME_UTC) == TIME_UTC);

    struct timeval timeval;

    __astia_time_host_clock_id = -1;

    CHECK(gettimeofday(&timeval, NULL) == 0 && timeval.tv_sec > 0 &&
          timeval.tv_usec >= 0 && timeval.tv_usec < 1000000);
    CHECK(__astia_time_host_clock_id == CLOCK_REALTIME);

    errno = 0;

    CHECK(gettimeofday(NULL, NULL) == -1 && errno == EINVAL);
}

static void test_signal_surface(void) {
    CHECK(STDOUT_FILENO == 1);
    CHECK(sysconf(_SC_REALTIME_SIGNALS) == -1);
}

int main(void) {
    test_stream_helpers();
    test_conversions_and_strings();
    test_printf_family();
    test_scan_family();
    test_c_locale();
    test_process_hooks();
    test_wide_c_locale();
    test_utc_time();
    test_signal_surface();

    return fails != 0;
}
