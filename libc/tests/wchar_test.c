#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <wchar.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_mb_cur_max(void) {
    CHECK(MB_CUR_MAX == 1);
}

static void test_mbrlen(void) {
    mbstate_t state = {0};
    char      ascii = 'A';
    char      nul   = '\0';
    char      high  = (char) 0x80;

    state.pending = 1;
    CHECK(mbrlen(NULL, 0, &state) == 0);
    CHECK(state.pending == 0);

    CHECK(mbrlen(&nul, 1, NULL) == 0);
    CHECK(mbrlen(&ascii, 1, NULL) == 1);
    CHECK(mbrlen(&ascii, 0, NULL) == (size_t) -2);

    errno = 0;
    CHECK(mbrlen(&high, 1, NULL) == (size_t) -1);
    CHECK(errno == EILSEQ);
}

static void test_ascii_conversion(void) {
    mbstate_t state  = {0};
    wchar_t   wc     = 0;
    char      buf[2] = {0, 0};
    char      high   = (char) 0x80;

    CHECK(mbrtowc(&wc, "A", 1, &state) == 1 && wc == L'A');
    CHECK(mbrtowc(&wc, "", 1, &state) == 0 && wc == L'\0');
    CHECK(mbrtowc(&wc, "A", 0, &state) == (size_t) -2);

    errno = 0;
    CHECK(mbrtowc(&wc, &high, 1, &state) == (size_t) -1);
    CHECK(errno == EILSEQ);

    CHECK(wcrtomb(buf, L'A', &state) == 1 && buf[0] == 'A');
    CHECK(wcrtomb(NULL, L'A', &state) == 1);

    errno = 0;
    CHECK(wcrtomb(buf, (wchar_t) 0xe9, &state) == (size_t) -1);
    CHECK(errno == EILSEQ);
}

static void test_wcwidth(void) {
    int c;

    CHECK(wcwidth(L'\0') == 0);

    for (c = 0x20; c <= 0x7e; c++)
        CHECK(wcwidth((wchar_t) c) == 1);

    for (c = 0x01; c <= 0x1f; c++)
        CHECK(wcwidth((wchar_t) c) == -1);

    CHECK(wcwidth((wchar_t) 0x7f) == -1);
    CHECK(wcwidth((wchar_t) 0x80) == -1);
    CHECK(wcwidth((wchar_t) 0xe9) == -1);
    CHECK(wcwidth((wchar_t) 0x1f600) == -1);
}

int main(void) {
    test_mb_cur_max();
    test_mbrlen();
    test_ascii_conversion();
    test_wcwidth();

    if (fails)
        printf("wchar_test: %d failure(s)\n", fails);
    return fails != 0;
}
