#include <ctype.h>
#include <stdio.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_classes(void) {
    CHECK(isdigit('0') && isdigit('9') && !isdigit('/') && !isdigit(':'));
    CHECK(isalpha('a') && isalpha('Z') && !isalpha('0') && !isalpha('@'));
    CHECK(isalpha('[') == 0);
    CHECK(isalnum('a') && isalnum('7') && !isalnum('!'));
    CHECK(isspace(' ') && isspace('\t') && isspace('\n') && isspace('\v') &&
          isspace('\f') && isspace('\r') && !isspace('x'));
    CHECK(isblank(' ') && isblank('\t') && !isblank('\n'));
    CHECK(isupper('A') && !isupper('a') && !isupper('1'));
    CHECK(islower('a') && !islower('A') && !islower('_'));
    CHECK(isxdigit('0') && isxdigit('f') && isxdigit('F') && !isxdigit('g') &&
          !isxdigit('G'));
    CHECK(iscntrl('\n') && iscntrl(0x7f) && !iscntrl('a'));
    CHECK(isprint(' ') && isprint('~') && !isprint(0x7f));
    CHECK(isgraph('!') && !isgraph(' ') && !isgraph(0x7f));
    CHECK(ispunct('!') && ispunct('.') && !ispunct('a') && !ispunct('0') &&
          !ispunct(' '));
}

static void test_edges(void) {
    /* C locale: bytes >= 0x80 are not letters, digits, or space */
    CHECK(!isalpha(0xE9) && !isdigit(0xB2) && !isupper(0xC1));

    /* EOF classifies as "none of the above", which is the C-locale answer */
    CHECK(!isalnum(EOF) && !isalpha(EOF) && !isspace(EOF) && !isdigit(EOF) &&
          !isprint(EOF) && !iscntrl(EOF) && !isxdigit(EOF) && !isgraph(EOF));
}

static void test_case(void) {
    CHECK(tolower('A') == 'a');
    CHECK(tolower('a') == 'a');
    CHECK(tolower('0') == '0');
    CHECK(tolower(EOF) == EOF);
    CHECK(toupper('z') == 'Z');
    CHECK(toupper('Z') == 'Z');
    CHECK(toupper(' ') == ' ');

    /* round trip over every ASCII letter */
    for (int c = 'A'; c <= 'Z'; c++)
        CHECK(toupper(tolower((char) c)) == c);
    for (int c = 'a'; c <= 'z'; c++)
        CHECK(tolower(toupper((char) c)) == c);
}

int main(void) {
    test_classes();
    test_edges();
    test_case();

    if (fails)
        printf("ctype_test: %d failure(s)\n", fails);
    return fails != 0;
}
