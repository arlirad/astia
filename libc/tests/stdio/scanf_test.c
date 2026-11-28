#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

#define CANARY 0xA5A5A5A5u

static int wide_equal(const wchar_t* text, const wchar_t* expect) {
    while (*expect != L'\0') {
        if (*text != *expect)
            return 0;

        text++;
        expect++;
    }

    return *text == L'\0';
}

#define PUSHBACK_PATH "/tmp/astia_scanf_pushback.dat"

extern int unlink(const char* path);

static FILE* open_with(const char* text) {
    FILE* f = fopen(PUSHBACK_PATH, "w");
    if (f == NULL)
        return NULL;

    fputs(text, f);
    fclose(f);

    return fopen(PUSHBACK_PATH, "r");
}

static void test_integer_prefixes(void) {
    int      n;
    int      pos;
    unsigned u;

    n   = 0;
    pos = -1;

    CHECK(sscanf("12abc", "%d%n", &n, &pos) == 1);
    CHECK(n == 12 && pos == 2);

    CHECK(sscanf("-12abc", "%d%n", &n, &pos) == 1);
    CHECK(n == -12 && pos == 3);

    CHECK(sscanf("+7x", "%d%n", &n, &pos) == 1);
    CHECK(n == 7 && pos == 2);

    CHECK(sscanf("12.5", "%d%n", &n, &pos) == 1);
    CHECK(n == 12 && pos == 2);

    CHECK(sscanf("0x10zz", "%x%n", &u, &pos) == 1);
    CHECK(u == 16 && pos == 4);

    CHECK(sscanf("0x10zz", "%i%n", &n, &pos) == 1);
    CHECK(n == 16 && pos == 4);

    CHECK(sscanf("17x", "%o%n", &u, &pos) == 1);
    CHECK(u == 15 && pos == 2);

    CHECK(sscanf("42kg", "%u%n", &u, &pos) == 1);
    CHECK(u == 42 && pos == 2);

    CHECK(sscanf("0x", "%x%n", &u, &pos) == 1);
    CHECK(u == 0 && pos == 1);

    n   = 0;
    pos = -1;

    CHECK(sscanf("abc", "%d%n", &n, &pos) == 0);
    CHECK(n == 0 && pos == -1);

    CHECK(sscanf("12abc", "%1d%n", &n, &pos) == 1);
    CHECK(n == 1 && pos == 1);
}

static void test_float_prefixes(void) {
    float  f;
    double d;
    int    pos;

    CHECK(sscanf("3.5x", "%f%n", &f, &pos) == 1);
    CHECK(f == 3.5f && pos == 3);

    CHECK(sscanf("3.5x", "%2f%n", &f, &pos) == 1);
    CHECK(f == 3.0f && pos == 2);

    CHECK(sscanf("-2.5e3z", "%lf%n", &d, &pos) == 1);
    CHECK(d == -2500.0 && pos == 6);

    CHECK(sscanf("0x1.8p+1zz", "%lf%n", &d, &pos) == 1);
    CHECK(d == 3.0 && pos == 8);

    CHECK(sscanf("1e", "%f%n", &f, &pos) == 1);
    CHECK(f == 1.0f && pos == 1);

    CHECK(sscanf("1.2.3", "%lf%n", &d, &pos) == 1);
    CHECK(d == 1.2 && pos == 3);

    f   = 0.0f;
    pos = -1;

    CHECK(sscanf("abc", "%f%n", &f, &pos) == 0);
    CHECK(f == 0.0f && pos == -1);
}

static void test_pointer_prefix(void) {
    void* ptr = NULL;

    int pos = -1;

    CHECK(sscanf("0x10zz", "%p%n", &ptr, &pos) == 1);
    CHECK((uintptr_t) ptr == 16 && pos == 4);
}

static int canaries_intact(const uint32_t* lead, const uint32_t* tail) {
    return lead[0] == CANARY && lead[1] == CANARY && tail[0] == CANARY &&
           tail[1] == CANARY;
}

static void test_wide_char(void) {
    struct {
        uint32_t lead[2];
        wint_t   chars[4];
        uint32_t tail[2];
    } box;

    wint_t wc;

    wc = 0;

    memset(&box, 0xA5, sizeof box);

    CHECK(sscanf("abc", "%lc", &wc) == 1);
    CHECK(wc == (wint_t) L'a');

    CHECK(sscanf("abc", "%3lc", &box.chars[0]) == 1);
    CHECK(box.chars[0] == (wint_t) L'a' && box.chars[1] == (wint_t) L'b' &&
          box.chars[2] == (wint_t) L'c');
    CHECK(box.chars[3] == (wint_t) CANARY);
    CHECK(canaries_intact(box.lead, box.tail));

    {
        int pos = -1;

        CHECK(sscanf("abc", "%2lc%n", &box.chars[0], &pos) == 1);
        CHECK(box.chars[0] == (wint_t) L'a' && box.chars[1] == (wint_t) L'b' && pos == 2);
    }

    CHECK(sscanf(" x", "%lc", &wc) == 1);
    CHECK(wc == (wint_t) L' ');

    CHECK(sscanf("abc", "%*lc%lc", &wc) == 1);
    CHECK(wc == (wint_t) L'b');

    CHECK(sscanf("", "%lc", &wc) == EOF);

    errno = 0;
    wc    = (wint_t) 0x12345678u;

    CHECK(sscanf("\xc3\xa9", "%lc", &wc) == 0);
    CHECK(errno == EILSEQ);
    CHECK(wc == (wint_t) 0x12345678u);
}

static void test_wide_string(void) {
    struct {
        uint32_t lead[2];
        wchar_t  text[8];
        uint32_t tail[2];
    } box;

    struct {
        uint32_t lead[2];
        wchar_t  text[4];
        uint32_t tail[2];
    } fit;

    int pos;

    memset(&box, 0xA5, sizeof box);

    CHECK(sscanf("abc", "%ls", box.text) == 1);
    CHECK(wide_equal(box.text, L"abc"));

    memset(&box, 0xA5, sizeof box);

    CHECK(sscanf("  abc def", "%ls%n", box.text, &pos) == 1);
    CHECK(wide_equal(box.text, L"abc") && pos == 5);

    CHECK(sscanf("abcdef", "%3ls", box.text) == 1);
    CHECK(wide_equal(box.text, L"abc"));

    CHECK(sscanf("abc", "%ls", box.text) == 1);
    CHECK(wide_equal(box.text, L"abc"));

    CHECK(sscanf("abc123", "%l[a-c]%n", box.text, &pos) == 1);
    CHECK(wide_equal(box.text, L"abc") && pos == 3);

    CHECK(sscanf("ab12", "%l[^0-9]", box.text) == 1);
    CHECK(wide_equal(box.text, L"ab"));

    CHECK(sscanf("abc def", "%*ls%ls", box.text) == 1);
    CHECK(wide_equal(box.text, L"def"));

    CHECK(sscanf("", "%ls", box.text) == EOF);
    CHECK(sscanf("   ", "%ls", box.text) == EOF);

    memset(&box, 0xA5, sizeof box);

    errno = 0;

    CHECK(sscanf("a\xc3\xa9", "%ls", box.text) == 1);
    CHECK(errno == EILSEQ);
    CHECK(wide_equal(box.text, L"a"));
    CHECK(box.text[2] == (wchar_t) CANARY);
    CHECK(canaries_intact(box.lead, box.tail));

    memset(&box, 0xA5, sizeof box);

    errno = 0;

    CHECK(sscanf("\xc3\xa9", "%ls", box.text) == 0);
    CHECK(errno == EILSEQ);
    CHECK(box.text[0] == (wchar_t) CANARY);
    CHECK(canaries_intact(box.lead, box.tail));

    memset(&fit, 0xA5, sizeof fit);

    CHECK(sscanf("abcd", "%3ls", fit.text) == 1);
    CHECK(wide_equal(fit.text, L"abc"));
    CHECK(canaries_intact(fit.lead, fit.tail));
}

static void test_narrow_unchanged(void) {
    char text[8] = {0};
    int  pos     = -1;

    CHECK(sscanf("abc", "%s%n", text, &pos) == 1);
    CHECK(strcmp(text, "abc") == 0 && pos == 3);

    CHECK(sscanf("ab", "%2c", text) == 1);
    CHECK(text[0] == 'a' && text[1] == 'b');

    CHECK(sscanf("abc", "%3c", text) == 1);
    CHECK(text[0] == 'a' && text[1] == 'b' && text[2] == 'c');
}

static void test_file_pushback(void) {
    static char buf[2];

    FILE* f;

    char c;
    char word[16];
    int  n;

    (void) unlink(PUSHBACK_PATH);

    f = open_with("12x");

    CHECK(f != NULL);

    if (f != NULL) {
        n = 0;
        c = '\0';

        CHECK(fscanf(f, "%d%c", &n, &c) == 2);
        CHECK(n == 12 && c == 'x');
        CHECK(fgetc(f) == EOF);
        CHECK(fclose(f) == 0);
    }

    f = open_with("12abc");

    CHECK(f != NULL);

    if (f != NULL) {
        n = 0;
        c = '\0';

        CHECK(fscanf(f, "%d%c", &n, &c) == 2);
        CHECK(n == 12 && c == 'a');
        CHECK(ftell(f) == 3);
        CHECK(fgetc(f) == 'b');
        CHECK(fgetc(f) == 'c');
        CHECK(fgetc(f) == EOF);
        CHECK(ftell(f) == 5);
        CHECK(fclose(f) == 0);
    }

    f = open_with("7fxyz");

    CHECK(f != NULL);

    if (f != NULL) {
        n       = 0;
        word[0] = '\0';

        CHECK(fscanf(f, "%d%[a-z]", &n, word) == 2);
        CHECK(n == 7 && strcmp(word, "fxyz") == 0);
        CHECK(fclose(f) == 0);
    }

    f = open_with("7x");

    CHECK(f != NULL);

    if (f != NULL) {
        n = 0;

        CHECK(ungetc('Q', f) == 'Q');
        CHECK(fscanf(f, "%d", &n) == 0);
        CHECK(fgetc(f) == 'Q');
        CHECK(ftell(f) == 0);
        CHECK(fgetc(f) == '7');
        CHECK(fgetc(f) == 'x');
        CHECK(ftell(f) == 2);
        CHECK(fclose(f) == 0);
    }

    f = open_with("123456abc");

    CHECK(f != NULL);

    if (f != NULL) {
        CHECK(setvbuf(f, buf, _IOFBF, sizeof buf) == 0);

        n       = 0;
        word[0] = '\0';

        CHECK(fscanf(f, "%d%s", &n, word) == 2);
        CHECK(n == 123456 && strcmp(word, "abc") == 0);
        CHECK(fgetc(f) == EOF);
        CHECK(fclose(f) == 0);
    }

    (void) unlink(PUSHBACK_PATH);
}

int main(void) {
    test_integer_prefixes();
    test_float_prefixes();
    test_pointer_prefix();
    test_wide_char();
    test_wide_string();
    test_narrow_unchanged();
    test_file_pushback();

    return fails != 0;
}
