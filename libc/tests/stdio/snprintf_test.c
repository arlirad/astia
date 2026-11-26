#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void check_fmt(const char* got, int got_ret, const char* want, int want_ret,
                      int line) {
    if (got_ret != want_ret || strcmp(got, want) != 0) {
        printf("FAIL snprintf_test:%d: got \"%s\" ret %d, "
               "want \"%s\" ret %d\n",
               line, got, got_ret, want, want_ret);
        fails++;
    }
}

/* snprintf behind a macro so every case is a compile-time-checked format */
#define CHECK_FMT(want, want_ret, ...)                        \
    do {                                                      \
        char buf_[256];                                       \
        int  ret_ = snprintf(buf_, sizeof buf_, __VA_ARGS__); \
        check_fmt(buf_, ret_, (want), (want_ret), __LINE__);  \
    } while (0)

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static int wrap_vsnprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap;

    int r;

    va_start(ap, fmt);

    r = vsnprintf(buf, n, fmt, ap);

    va_end(ap);

    return r;
}

static int wrap_vsprintf(char* buf, const char* fmt, ...) {
    va_list ap;

    int r;

    va_start(ap, fmt);

    r = vsprintf(buf, fmt, ap);

    va_end(ap);

    return r;
}

static void test_ints(void) {
    CHECK_FMT("42", 2, "%d", 42);
    CHECK_FMT("0", 1, "%d", 0);
    CHECK_FMT("-17", 3, "%d", -17);
    CHECK_FMT("+7", 2, "%+d", 7);
    CHECK_FMT(" 7", 2, "% d", 7);
    CHECK_FMT("4294967295", 10, "%u", 4294967295u);
    CHECK_FMT("18446744073709551615", 20, "%llu", ULLONG_MAX);
    CHECK_FMT("9223372036854775807", 19, "%lld", LLONG_MAX);
    CHECK_FMT("-9223372036854775808", 20, "%lld", LLONG_MIN);
    CHECK_FMT("123", 3, "%zu", (size_t) 123);
    CHECK_FMT("4660", 4, "%hd", 4660); /* h: value through a short */
    CHECK_FMT("44", 2, "%hhd", 300);   /* hh: through a signed char */
    CHECK_FMT("-1", 2, "%hhd", -1);
    CHECK_FMT("7", 1, "%i", 7);
}

static void test_radices(void) {
    CHECK_FMT("ff", 2, "%x", 255);
    CHECK_FMT("FF", 2, "%X", 255);
    CHECK_FMT("0xff", 4, "%#x", 255);
    CHECK_FMT("0XFF", 4, "%#X", 255);
    CHECK_FMT("010", 3, "%#o", 8);
    CHECK_FMT("0", 1, "%#o", 0); /* already has its zero */
    CHECK_FMT("10", 2, "%o", 8);
    CHECK_FMT("0", 1, "%x", 0); /* no prefix on zero */
}

static void test_width_and_flags(void) {
    CHECK_FMT("   42", 5, "%5d", 42);
    CHECK_FMT("42   ", 5, "%-5d", 42);
    CHECK_FMT("00042", 5, "%05d", 42);
    CHECK_FMT("-0003", 5, "%05d", -3); /* sign before the zeros */
    CHECK_FMT("+0042", 5, "%+05d", 42);
    CHECK_FMT("  0042", 6, "%6.4d", 42); /* precision, then spaces */
    CHECK_FMT("007", 3, "%03d", 7);

    /* These two combinations are *intentionally* unusual — the standard
       says the 0 flag loses to '-' and to a precision — so the format
       goes through a variable: gcc's checker would flag the literals as
       if they were mistakes. */
    {
        const char* f_left_zero = "%-05d";
        const char* f_prec_zero = "%08.3d";

        CHECK_FMT("42   ", 5, f_left_zero, 42); /* 0 loses to - */
        /* precision kills the 0 flag: spaces pad, digits do not */
        CHECK_FMT("     007", 8, f_prec_zero, 7);
    }

    CHECK_FMT("007   ", 6, "%-6.3d", 7);

    /* star width and precision */
    CHECK_FMT("   42", 5, "%*d", 5, 42);
    CHECK_FMT("7   ", 4, "%*d", -4, 7); /* negative width = left */
    CHECK_FMT("007", 3, "%.*d", 3, 7);
    CHECK_FMT("7", 1, "%.*d", -1, 7); /* negative precision = none */
}

static void test_chars_and_strings(void) {
    CHECK_FMT("A", 1, "%c", 'A');
    CHECK_FMT("   A", 4, "%4c", 'A');
    CHECK_FMT("A   ", 4, "%-4c", 'A');
    CHECK_FMT("100%", 4, "100%%");
    CHECK_FMT("hello", 5, "%s", "hello");
    CHECK_FMT("abc", 3, "%.3s", "abcdef");
    CHECK_FMT("ab", 2, "%.2s", "abcdef");
    CHECK_FMT("", 0, "%.0s", "x");
    CHECK_FMT("abcdef  ", 8, "%-8s", "abcdef");
    CHECK_FMT("  abcdef", 8, "%8s", "abcdef");

    {
        const char* nil = NULL;

        CHECK_FMT("(null)", 6, "%s", nil);
    }
}

static void test_pointers(void) {
    CHECK_FMT("0x1234", 6, "%p", (void*) 0x1234); /* astia form, always 0x */
    CHECK_FMT("0x0", 3, "%p", (void*) 0);
}

static void test_unsupported(void) {
    CHECK_FMT("1.500000", 8, "%f", 1.5);
    CHECK_FMT("2.500000 42", 11, "%f %d", 2.5, 42);
    CHECK_FMT("1.50", 4, "%.2f", 1.5);
    CHECK_FMT("1.25e+00", 8, "%.2e", 1.25);
    CHECK_FMT("1.25", 4, "%.3g", 1.25);
}

static void test_truncation(void) {
    char            small[5];
    volatile size_t cap_5 = 5; /* bounds via variables: truncation is
                                  exactly what gcc's -Wformat-
                                  truncation would warn about */
    volatile size_t cap_1 = 1;
    volatile size_t cap_0 = 0;
    int             r;

    r = snprintf(small, cap_5, "%s", "hello");

    CHECK(r == 5);                     /* would-be length */
    CHECK(strcmp(small, "hell") == 0); /* truncated, terminated */

    r = snprintf(small, cap_1, "%s", "hi");

    CHECK(r == 2 && small[0] == '\0'); /* only the terminator fits */

    r = snprintf(small, cap_0, "%s", "hi");

    CHECK(r == 2); /* nothing may be written */

    r = snprintf(NULL, 0, "%d", 12345);

    CHECK(r == 5); /* pure length computation */

    r = snprintf(small, cap_5, "%.100d", 7);

    CHECK(r == 100);
    CHECK(strcmp(small, "0000") == 0); /* first bytes of the zeros */
}

static void test_entry_points(void) {
    char b[64];
    int  r;

    r = sprintf(b, "%03d/%s", 7, "x");

    CHECK(r == 5 && strcmp(b, "007/x") == 0);

    r = wrap_vsprintf(b, "%s!", "ok"); /* the va_list entry point */

    CHECK(r == 3 && strcmp(b, "ok!") == 0);

    r = wrap_vsnprintf(b, sizeof b, "%d-%s", 9, "z");

    CHECK(r == 3 && strcmp(b, "9-z") == 0);

    {
        const char* empty_fmt = ""; /* via a variable: an empty
   literal at a call site is a -Wformat-zero-length mistake
   signal, and here it is the case under test */
        CHECK(snprintf(NULL, 0, empty_fmt) == 0);
    }
}

static void test_long(void) {
    char big[512];
    int  r;

    r = snprintf(big, sizeof big, "%s-%d-%s", "tag", -7, "end");

    CHECK(r == 10 && strcmp(big, "tag--7-end") == 0);

    /* 400 digits-worth of padding must count toward the return */
    r = snprintf(big, sizeof big, "%400d", 1);

    CHECK(r == 400);
    CHECK(big[0] == ' ' && big[399] == '1' && big[400] == '\0');
}

int main(void) {
    test_ints();
    test_radices();
    test_width_and_flags();
    test_chars_and_strings();
    test_pointers();
    test_unsupported();
    test_truncation();
    test_entry_points();
    test_long();

    if (fails)
        printf("snprintf_test: %d failure(s)\n", fails);

    return fails != 0;
}
