#pragma once

/*
 * astia libc — <assert.h>
 *
 * A failed assertion prints through the runtime printf (host libc in
 * tests; the astia CRT once it exists) and then aborts, which is
 * currently a trap instruction.  NDEBUG strips the check entirely.
 */

#ifdef __cplusplus
extern "C" {
#endif

void __astia_assert_fail(const char* expr, const char* file, int line, const char* func);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef NDEBUG
#define assert(expr) ((void) 0)
#else
#define assert(expr) \
    ((expr) ? (void) 0 : __astia_assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

/* C11 spells static_assert in this header; C++ has it as a keyword. */
#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#ifndef static_assert
#define static_assert(cond, msg) _Static_assert(cond, msg)
#endif
#endif
