#pragma once

char* getenv(const char* name);
int   setenv(const char* name, const char* value, int overwrite);
int   unsetenv(const char* name);
int   putenv(char* string);

/*
 * astia libc — <stdlib.h>
 *
 * Program control, conversions, searching/sorting, pseudo-random numbers.
 *
 * Environment storage is kept in the process image (the CRT supplies the
 * initial environ vector); the mutators copy that vector when it first needs
 * to grow.  This is enough for shell exports without a kernel environment
 * syscall.
 * Floating parsing (atof/strtod) waits for the float formatting layer.
 */

#include <stddef.h>

#ifndef ASTIA_NORETURN
#if defined(__cplusplus)
#define ASTIA_NORETURN [[noreturn]]
#else
#define ASTIA_NORETURN _Noreturn
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

/* memory */
void* malloc(size_t size);
void  free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);

/* program */
ASTIA_NORETURN void abort(void);
int                 atexit(void (*function)(void));
ASTIA_NORETURN void exit(int status);
ASTIA_NORETURN void _Exit(int status);

/* string conversions */
int                atoi(const char* nptr);
long               atol(const char* nptr);
long               strtol(const char* nptr, char** endptr, int base);
unsigned long      strtoul(const char* nptr, char** endptr, int base);
long long          strtoll(const char* nptr, char** endptr, int base);
unsigned long long strtoull(const char* nptr, char** endptr, int base);
double             strtod(const char* nptr, char** endptr);
float              strtof(const char* nptr, char** endptr);
long double        strtold(const char* nptr, char** endptr);
double             atof(const char* nptr);

/* math (the half that cannot silently overflow) */
int  abs(int j);
long labs(long j);

/* searching and sorting */
void  qsort(void* base, size_t nmemb, size_t size,
            int (*compar)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*));

/* pseudo-random numbers (implementation-defined sequence, per the standard) */
int  rand(void);
void srand(unsigned seed);

#ifdef __cplusplus
} /* extern "C" */
#endif
