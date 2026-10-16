#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WEOF
#define WEOF ((wint_t) - 1)
#endif

typedef unsigned int wint_t;
typedef struct {
    unsigned char pending;
} mbstate_t;

size_t         wcslen(const wchar_t* s);
int            wcscmp(const wchar_t* a, const wchar_t* b);
int            wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t*       wcscpy(wchar_t* dst, const wchar_t* src);
wchar_t*       wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t*       wcscat(wchar_t* dst, const wchar_t* src);
const wchar_t* wcschr(const wchar_t* s, wchar_t c);
const wchar_t* wcsrchr(const wchar_t* s, wchar_t c);
const wchar_t* wcsstr(const wchar_t* haystack, const wchar_t* needle);
size_t         wcsspn(const wchar_t* s, const wchar_t* accept);
size_t         wcscspn(const wchar_t* s, const wchar_t* reject);
const wchar_t* wcspbrk(const wchar_t* s, const wchar_t* accept);
wchar_t*       wcstok(wchar_t* s, const wchar_t* delimiters, wchar_t** state);

wchar_t* wmemcpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* wmemmove(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* wmemset(wchar_t* dst, wchar_t c, size_t n);
int      wmemcmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n);

int    btowc(int c);
int    wctob(wint_t c);
size_t mbrtowc(wchar_t* dst, const char* src, size_t n, mbstate_t* state);
size_t wcrtomb(char* dst, wchar_t wc, mbstate_t* state);
size_t mbsrtowcs(wchar_t* dst, const char** src, size_t n, mbstate_t* state);
size_t wcsrtombs(char* dst, const wchar_t** src, size_t n, mbstate_t* state);
int    mblen(const char* src, size_t n);
int    mbtowc(wchar_t* dst, const char* src, size_t n);
int    wctomb(char* dst, wchar_t wc);
size_t mbstowcs(wchar_t* dst, const char* src, size_t n);
size_t wcstombs(char* dst, const wchar_t* src, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif
