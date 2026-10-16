#pragma once

/*
 * astia libc — <string.h>
 *
 * The ISO C string and memory block functions.  The current locale is the
 * C locale, so strcoll/strxfrm are bytewise operations.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* memory blocks */
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
void* memchr(const void* s, int c, size_t n);

/* c strings */
size_t strlen(const char* s);
size_t strnlen(const char* s, size_t n); /* POSIX/C23, kept for ergonomics */
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
int    strcoll(const char* a, const char* b);
size_t strxfrm(char* dst, const char* src, size_t n);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, size_t n);
char*  strcat(char* dst, const char* src);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);
char*  strstr(const char* hay, const char* needle);
size_t strspn(const char* s, const char* accept);
size_t strcspn(const char* s, const char* reject);
char*  strpbrk(const char* s, const char* accept);
char*  strtok(char* s, const char* delim);       /* per-thread state (see source) */
char*  strdup(const char* s);                    /* POSIX */
char*  strndup(const char* s, size_t n);         /* POSIX */
int    strcasecmp(const char* a, const char* b); /* POSIX */
int    strncasecmp(const char* a, const char* b, size_t n); /* POSIX */
char*  strerror(int errnum);

#ifdef __cplusplus
} /* extern "C" */
#endif
