#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int mbsinit(const mbstate_t* state) {
    return state == NULL || state->pending == 0;
}

size_t wcslen(const wchar_t* s) {
    size_t n = 0;
    while (s[n] != L'\0')
        n++;
    return n;
}

int wcscmp(const wchar_t* a, const wchar_t* b) {
    while (*a != L'\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b ? 0 : (*a < *b ? -1 : 1);
}

int wcsncmp(const wchar_t* a, const wchar_t* b, size_t n) {
    while (n != 0 && *a != L'\0' && *a == *b) {
        a++;
        b++;
        n--;
    }
    return n == 0 || *a == *b ? 0 : (*a < *b ? -1 : 1);
}

wchar_t* wcscpy(wchar_t* dst, const wchar_t* src) {
    wchar_t* result = dst;
    while ((*dst++ = *src++) != L'\0')
        ;
    return result;
}

wchar_t* wcsncpy(wchar_t* dst, const wchar_t* src, size_t n) {
    size_t i = 0;
    while (i < n && src[i] != L'\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < n)
        dst[i++] = L'\0';
    return dst;
}

wchar_t* wcscat(wchar_t* dst, const wchar_t* src) {
    wcscpy(dst + wcslen(dst), src);
    return dst;
}

const wchar_t* wcschr(const wchar_t* s, wchar_t c) {
    do {
        if (*s == c)
            return s;
    } while (*s++ != L'\0');
    return NULL;
}

const wchar_t* wcsrchr(const wchar_t* s, wchar_t c) {
    const wchar_t* result = NULL;
    do {
        if (*s == c)
            result = s;
    } while (*s++ != L'\0');
    return result;
}

const wchar_t* wcsstr(const wchar_t* haystack, const wchar_t* needle) {
    size_t n = wcslen(needle);
    if (n == 0)
        return haystack;
    while (*haystack != L'\0') {
        if (wcsncmp(haystack, needle, n) == 0)
            return haystack;
        haystack++;
    }
    return NULL;
}

size_t wcsspn(const wchar_t* s, const wchar_t* accept) {
    size_t n = 0;
    while (s[n] != L'\0' && wcschr(accept, s[n]) != NULL)
        n++;
    return n;
}

size_t wcscspn(const wchar_t* s, const wchar_t* reject) {
    size_t n = 0;
    while (s[n] != L'\0' && wcschr(reject, s[n]) == NULL)
        n++;
    return n;
}

const wchar_t* wcspbrk(const wchar_t* s, const wchar_t* accept) {
    while (*s != L'\0') {
        if (wcschr(accept, *s) != NULL)
            return s;
        s++;
    }
    return NULL;
}

wchar_t* wcstok(wchar_t* s, const wchar_t* delimiters, wchar_t** state) {
    wchar_t* end;

    if (s == NULL)
        s = *state;
    if (s == NULL)
        return NULL;
    s += wcsspn(s, delimiters);
    if (*s == L'\0') {
        *state = NULL;
        return NULL;
    }
    end = s + wcscspn(s, delimiters);
    if (*end != L'\0') {
        *end   = L'\0';
        *state = end + 1;
    }
    else {
        *state = NULL;
    }
    return s;
}

wchar_t* wmemcpy(wchar_t* dst, const wchar_t* src, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    return dst;
}

wchar_t* wmemmove(wchar_t* dst, const wchar_t* src, size_t n) {
    if (dst < src) {
        size_t i;
        for (i = 0; i < n; i++)
            dst[i] = src[i];
    }
    else if (dst > src) {
        while (n != 0) {
            n--;
            dst[n] = src[n];
        }
    }
    return dst;
}

wchar_t* wmemset(wchar_t* dst, wchar_t c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        dst[i] = c;
    return dst;
}

int wmemcmp(const wchar_t* a, const wchar_t* b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] == c)
            return (wchar_t*) (s + i);
    return NULL;
}

int btowc(int c) {
    if (c == EOF)
        return WEOF;
    return (unsigned char) c < 0x80 ? (unsigned char) c : WEOF;
}

int wctob(wint_t c) {
    return c < 0x80 ? (int) c : EOF;
}

size_t mbrtowc(wchar_t* dst, const char* src, size_t n, mbstate_t* state) {
    (void) state;
    if (src == NULL)
        return 0;
    if (n == 0)
        return (size_t) -2;
    if ((unsigned char) src[0] >= 0x80) {
        errno = EILSEQ;
        return (size_t) -1;
    }
    if (dst != NULL)
        *dst = (wchar_t) (unsigned char) src[0];
    return src[0] == '\0' ? 0 : 1;
}

size_t wcrtomb(char* dst, wchar_t wc, mbstate_t* state) {
    (void) state;
    if (dst == NULL)
        return 1;
    if (wc > 0x7f) {
        errno = EILSEQ;
        return (size_t) -1;
    }
    *dst = (char) wc;
    return 1;
}

size_t mbsrtowcs(wchar_t* dst, const char** src, size_t n, mbstate_t* state) {
    size_t count = 0;
    while (dst == NULL || count < n) {
        size_t result =
            mbrtowc(dst == NULL ? NULL : dst + count, *src, (size_t) 1, state);
        if (result == (size_t) -1 || result == (size_t) -2)
            return (size_t) -1;
        if (dst == NULL)
            count++;
        else
            count++;
        if (**src == '\0') {
            if (src != NULL)
                *src = NULL;
            return count - 1;
        }
        (*src)++;
    }
    return count;
}

size_t wcsrtombs(char* dst, const wchar_t** src, size_t n, mbstate_t* state) {
    size_t count = 0;
    while (dst == NULL || count < n) {
        wchar_t wc = **src;
        if (wc > 0x7f) {
            errno = EILSEQ;
            return (size_t) -1;
        }
        if (dst != NULL)
            dst[count] = (char) wc;
        count++;
        if (wc == L'\0') {
            *src = NULL;
            return count - 1;
        }
        (*src)++;
    }
    (void) state;
    return count;
}

int mblen(const char* src, size_t n) {
    size_t result = mbrtowc(NULL, src, n, NULL);
    return result == (size_t) -1 || result == (size_t) -2
               ? (result == (size_t) -2 ? -1 : -1)
               : (int) result;
}

int mbtowc(wchar_t* dst, const char* src, size_t n) {
    size_t result = mbrtowc(dst, src, n, NULL);
    return result == (size_t) -1 || result == (size_t) -2 ? -1 : (int) result;
}

int wctomb(char* dst, wchar_t wc) {
    size_t result = wcrtomb(dst, wc, NULL);
    return result == (size_t) -1 ? -1 : (int) result;
}

size_t mbstowcs(wchar_t* dst, const char* src, size_t n) {
    const char* p = src;
    return mbsrtowcs(dst, &p, n, NULL);
}

size_t wcstombs(char* dst, const wchar_t* src, size_t n) {
    const wchar_t* p = src;
    return wcsrtombs(dst, &p, n, NULL);
}

static int ascii_wctype(wint_t c, wctype_t desc) {
    if (c > 0x7f)
        return 0;
    switch (desc) {
    case 1:
        return isalnum((int) c);
    case 2:
        return isalpha((int) c);
    case 3:
        return isblank((int) c);
    case 4:
        return iscntrl((int) c);
    case 5:
        return isdigit((int) c);
    case 6:
        return isgraph((int) c);
    case 7:
        return islower((int) c);
    case 8:
        return isprint((int) c);
    case 9:
        return ispunct((int) c);
    case 10:
        return isspace((int) c);
    case 11:
        return isupper((int) c);
    case 12:
        return isxdigit((int) c);
    default:
        return 0;
    }
}

#define WCTYPE_FN(name, desc)         \
    int name(wint_t c) {              \
        return ascii_wctype(c, desc); \
    }
WCTYPE_FN(iswalnum, 1)
WCTYPE_FN(iswalpha, 2)
WCTYPE_FN(iswblank, 3)
WCTYPE_FN(iswcntrl, 4)
WCTYPE_FN(iswdigit, 5)
WCTYPE_FN(iswgraph, 6)
WCTYPE_FN(iswlower, 7)
WCTYPE_FN(iswprint, 8)
WCTYPE_FN(iswpunct, 9)
WCTYPE_FN(iswspace, 10)
WCTYPE_FN(iswupper, 11)
WCTYPE_FN(iswxdigit, 12)

wint_t towlower(wint_t c) {
    return c < 128 ? (wint_t) tolower((int) c) : c;
}
wint_t towupper(wint_t c) {
    return c < 128 ? (wint_t) toupper((int) c) : c;
}

wctype_t wctype(const char* name) {
    static const char* const names[] = {"",      "alnum", "alpha", "blank", "cntrl",
                                        "digit", "graph", "lower", "print", "punct",
                                        "space", "upper", "xdigit"};
    size_t                   i;
    for (i = 1; i < sizeof names / sizeof names[0]; i++)
        if (strcmp(name, names[i]) == 0)
            return (wctype_t) i;
    return 0;
}

int iswctype(wint_t c, wctype_t desc) {
    return ascii_wctype(c, desc);
}

wctrans_t wctrans(const char* name) {
    if (strcmp(name, "tolower") == 0)
        return 1;
    if (strcmp(name, "toupper") == 0)
        return 2;
    return 0;
}

wint_t towctrans(wint_t c, wctrans_t desc) {
    return desc == 1 ? towlower(c) : desc == 2 ? towupper(c) : c;
}
