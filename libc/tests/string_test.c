#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_mem(void) {
    char        a[16];
    char        b[16];
    char        c[16];
    const char* h = "hello";

    memset(a, 0xAB, sizeof a);
    CHECK((unsigned char) a[0] == 0xAB && (unsigned char) a[15] == 0xAB);

    memcpy(a, "hello", 6); /* includes the terminator */
    CHECK(strcmp(a, "hello") == 0);
    CHECK(memcmp(a, "hello", 6) == 0);
    CHECK(memcmp("abc", "abd", 3) < 0);
    CHECK(memcmp("abd", "abc", 3) > 0);
    CHECK(memcmp("abc", "ab", 3) > 0); /* 'c' vs terminator */
    CHECK(memcmp("abc", "abc", 0) == 0);

    strcpy(b, "abcdefgh");
    memmove(b + 2, b, 6); /* overlapping, forward */
    CHECK(strcmp(b, "ababcdef") == 0);
    memmove(b, b + 2, 6); /* overlapping, backward */
    CHECK(strncmp(b, "abcdef", 6) == 0);

    strcpy(c, "xyz");
    memmove(c + 1, c, 4); /* shifts the NUL too */
    CHECK(strcmp(c, "xxyz") == 0);

    CHECK(memchr(h, 'l', 5) != NULL);
    CHECK(memchr(h, 'z', 5) == NULL);
    CHECK(memchr(h, '\0', 6) != NULL); /* terminator is findable */
    CHECK(memchr(h, 'x', 0) == NULL);
}

static void test_cstrings(void) {
    char        d[16];
    const char* s = "abcabc";
    const char* hit;

    CHECK(strlen("") == 0);
    CHECK(strlen("hello") == 5);
    CHECK(strnlen("hello", 3) == 3);
    CHECK(strnlen("hi", 10) == 2);
    CHECK(strnlen("", 5) == 0);

    CHECK(strcmp("abc", "abc") == 0);
    CHECK(strcmp("abc", "abd") < 0);
    CHECK(strcmp("abd", "abc") > 0);
    CHECK(strcmp("ab", "abc") < 0);
    CHECK(strcmp("abc", "ab") > 0);
    /* byte-wise comparison must be unsigned: 0x80 > 0x01 */
    CHECK(strcmp("\x80", "\x01") > 0);

    CHECK(strncmp("abc", "abd", 2) == 0);
    CHECK(strncmp("abc", "abd", 3) < 0);
    CHECK(strncmp("abc", "abc", 10) == 0);
    CHECK(strncmp("abc", "x", 0) == 0);

    strcpy(d, "abc");
    strcat(d, "def");
    CHECK(strcmp(d, "abcdef") == 0);
    strcat(d, "");
    CHECK(strcmp(d, "abcdef") == 0);

    memset(d, '?', sizeof d);
    strncpy(d, "ab", 5); /* pads with terminators */
    CHECK(d[0] == 'a' && d[1] == 'b' && d[2] == 0 && d[3] == 0 && d[4] == 0);

    memset(d, '?', sizeof d);
    { /* variable length: the non-terminating copy is the point of the
         test, and gcc's -Wstringop-truncation would flag the constant */
        volatile size_t three = 3;
        strncpy(d, "abcdef", three);
    }
    CHECK(d[0] == 'a' && d[1] == 'b' && d[2] == 'c' && d[3] == '?');

    CHECK(strchr(s, 'b') == s + 1);
    CHECK(strchr(s, 'z') == NULL);
    CHECK(strchr(s, '\0') == s + 6); /* terminator itself */
    CHECK(strrchr(s, 'b') == s + 4);
    CHECK(strrchr(s, 'a') == s + 3);
    CHECK(strrchr(s, 'z') == NULL);

    hit = strstr("hello world", "wor");
    CHECK(hit != NULL && strcmp(hit, "world") == 0);
    CHECK(strstr("hello", "xyz") == NULL);
    CHECK(strstr(s, "") == s);      /* empty needle: the haystack */
    hit = strstr("hello", "hello"); /* full match at offset 0 */
    CHECK(hit != NULL && strcmp(hit, "hello") == 0);
    CHECK(strstr("hello", "lo") != NULL);
    CHECK(strstr("ababab", "bab") != NULL);
    CHECK(strstr("aaa", "aab") == NULL); /* near-miss backtracking */

    CHECK(strspn("   abc", " ") == 3);
    CHECK(strspn("abc", " ") == 0);
    CHECK(strspn("", "abc") == 0);
    CHECK(strcspn("abc", "xyz") == 3);
    CHECK(strcspn("ab cd", " ") == 2);
    CHECK(strcspn("abc", "") == 3);
    hit = strpbrk("abc", "cx");
    CHECK(hit != NULL && *hit == 'c');
    CHECK(strpbrk("abc", "xyz") == NULL);
}

static void test_strtok(void) {
    char t[] = ",,a,b,,c,";

    /* leading and repeated delimiters are skipped; empty fields vanish */
    CHECK(strcmp(strtok(t, ","), "a") == 0);
    CHECK(strcmp(strtok(NULL, ","), "b") == 0);
    CHECK(strcmp(strtok(NULL, ","), "c") == 0);
    CHECK(strtok(NULL, ",") == NULL);
    CHECK(strtok(NULL, ",") == NULL); /* stays exhausted */
}

static void test_strings_aliases(void) {
    char        buffer[8] = "abc";
    const char* value     = "abc";
    CHECK(bcmp(buffer, "abc", 3) == 0);
    bcopy("xyz", buffer, 4);
    CHECK(strcmp(buffer, "xyz") == 0);
    bzero(buffer, sizeof buffer);
    CHECK(buffer[0] == 0 && buffer[7] == 0);
    CHECK(ffs(0) == 0 && ffs(1) == 1 && ffs(0x10) == 5);
    CHECK(index(value, 'b') == value + 1);
    CHECK(rindex("abca", 'a') != NULL && rindex("abca", 'a')[1] == '\0');
}

static void test_strerror(void) {
    CHECK(strerror(ENOENT) != NULL);
    CHECK(strcmp(strerror(ENOENT), "No such file or directory") == 0);
    CHECK(strcmp(strerror(ERANGE), "Numerical result out of range") == 0);
    CHECK(strcmp(strerror(EDOM), "Numerical argument out of domain") == 0);
    CHECK(strcmp(strerror(ENOTTY), "Inappropriate ioctl for device") == 0);
    CHECK(strcmp(strerror(123456), "Unknown error") == 0);
}

int main(void) {
    assert(1 == 1); /* header and fail-path link at least resolve */

    test_mem();
    test_cstrings();
    test_strtok();
    test_strings_aliases();
    test_strerror();

    if (fails)
        printf("string_test: %d failure(s)\n", fails);
    return fails != 0;
}
