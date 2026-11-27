#include "grep_regex.h"

#include <stdio.h>

typedef struct {
    const char* pattern;
    const char* text;
    int         insensitive;
    int         expected;
} regex_case;

static int match_case(const regex_case* item) {
    gre_regex regex;

    if (gre_compile(&regex, item->pattern, item->insensitive) != 0) {
        printf("grep regex: failed to compile [%s]\n", item->pattern);

        return 1;
    }

    int result = gre_match(&regex, item->text, strlen(item->text));

    gre_free(&regex);

    if (result != item->expected) {
        printf("grep regex: [%s] against [%s] returned %d, expected %d\n", item->pattern,
               item->text, result, item->expected);

        return 1;
    }

    return 0;
}

static int invalid_case(const char* pattern) {
    gre_regex regex;

    if (gre_compile(&regex, pattern, 0) == 0) {
        printf("grep regex: expected [%s] to be rejected\n", pattern);

        gre_free(&regex);

        return 1;
    }

    return 0;
}

int main(void) {
    static const regex_case cases[] = {
        {"", "", 0, 1},
        {"", "abc", 0, 1},
        {"abc", "xabcx", 0, 1},
        {"abc", "ab", 0, 0},
        {"a.c", "abc", 0, 1},
        {"a.c", "a.c", 0, 1},
        {"a.c", "ac", 0, 0},
        {"a.*c", "aXXc", 0, 1},
        {"ab*c", "ac", 0, 1},
        {"ab*c", "abbbc", 0, 1},
        {"ab*c", "adc", 0, 0},
        {"^root", "root:x", 0, 1},
        {"^root", "xroot", 0, 0},
        {"root$", "xroot\n", 0, 1},
        {"root$", "rootx\n", 0, 0},
        {"^$", "\n", 0, 1},
        {"^$", "x\n", 0, 0},
        {"^abc$", "abc\n", 0, 1},
        {"^abc$", "abcd\n", 0, 0},
        {"[abc]", "zzbzz", 0, 1},
        {"[^abc]", "zzz", 0, 1},
        {"[^abc]", "abc", 0, 0},
        {"[a-c]", "zbz", 0, 1},
        {"[a-c]", "zZz", 0, 0},
        {"[]a]", "]", 0, 1},
        {"[-a]", "-", 0, 1},
        {"[a-]", "-", 0, 1},
        {"[[:digit:]]", "a1b", 0, 1},
        {"[[:digit:]]", "abc", 0, 0},
        {"[[:space:]]", "a b", 0, 1},
        {"a\\.c", "a.c", 0, 1},
        {"a\\.c", "abc", 0, 0},
        {"a\\*c", "a*c", 0, 1},
        {"a\\*c", "abc", 0, 0},
        {"\\\\", "a\\b", 0, 1},
        {"\\(ab\\)\\1", "abab", 0, 1},
        {"\\(ab\\)\\1", "ab", 0, 0},
        {"\\(a\\)*\\1", "a", 0, 0},
        {"\\(a*\\)b\\1", "aabaa", 0, 1},
        {"^\\(a\\(b\\)*\\)*$", "abab", 0, 1},
        {"^\\(a\\(b\\)*\\)*\\2$", "abab", 0, 0},
        {"a\\{2,3\\}", "xaaay", 0, 1},
        {"a\\{2,3\\}", "xaay", 0, 1},
        {"a\\{2,3\\}", "xay", 0, 0},
        {"c\\{3\\}", "abcccd", 0, 1},
        {"c{3}", "c{3}", 0, 1},
        {"A.C", "abc", 1, 1},
        {"[a-z]", "Q", 1, 1},
        {"^ROOT", "root:x", 1, 1},
        {"c.", "abc\n", 0, 0},
        {"c$", "abc\n", 0, 1},
    };

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        if (match_case(&cases[i]) != 0)
            return 1;
    }

    static const char* invalid[] = {
        "a\\", "[abc", "\\(", "\\1", "a\\{3,2\\}", "a\\{256\\}",
    };

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        if (invalid_case(invalid[i]) != 0)
            return 1;
    }

    printf("grep regex tests ok\n");

    return 0;
}
