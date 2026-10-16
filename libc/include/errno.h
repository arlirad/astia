#pragma once

/*
 * astia libc — <errno.h>
 *
 * The codes are the userspace ABI's (<astia/api/errno.h>), a superset of
 * the three ISO C requires here: EDOM, EILSEQ, ERANGE.
 *
 * errno is thread-local from day one, exactly as the standard defines it
 * ("a macro that expands to a thread-local object").  Retrofitting this
 * after code has cached `&errno` or assumed a shared value is the classic
 * multithreading bug, so there is nothing to retrofit: one `_Thread_local`
 * int per thread, zero-initialized for free in .tbss.  When threads land,
 * the astia CRT must materialize PT_TLS for each thread's block and point
 * the thread register at it — making that a hard requirement of the
 * runtime is the point of doing it now.  Same story as any other TLS in
 * the process: errno is not special, it is just the one everybody reads.
 *
 * It is a macro (allowed, and common) rather than a bare object, which
 * also keeps it from colliding with a host libc's errno in the test
 * binaries — ours lives under a reserved name.
 */

#include <astia/api/errno.h>

#ifdef __cplusplus
extern "C" {
#if __cplusplus >= 201103L
extern thread_local int __astia_errno;
#else
extern int __astia_errno; /* pre-C++11: no thread_local; best effort */
#endif
#else
extern _Thread_local int __astia_errno;
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#define errno (__astia_errno)
