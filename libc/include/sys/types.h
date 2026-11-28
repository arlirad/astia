#pragma once

/*
 * astia libc — <sys/types.h>.  Only the types this libc's I/O surface
 * actually needs: ssize_t, a byte count that may be an error, off_t, a
 * file offset, and size_t, which POSIX puts here too and which is the
 * compiler's <stddef.h> rather than anything of ours.
 *
 * ssize_t and off_t are `long` on this target, which is what the ABI
 * spells them as (<astia/api/aio.h>'s iovec carries uint64_t lengths,
 * the SQE a uint64_t offset) and what the build machine's LP64 types
 * alias to, so the host ports in tests/ can include this header and
 * keep using their own headers for everything else.  Nothing else POSIX
 * puts here exists yet — no pid_t, no mode_t — and declaring a type no
 * prototype uses would only be a promise to keep.
 */

#include <stddef.h> /* size_t: the compiler's, in both builds */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef long          ssize_t;
typedef long          off_t;
typedef int           pid_t;
typedef unsigned int  mode_t;
typedef unsigned int  uid_t;
typedef unsigned int  gid_t;
typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned long nlink_t;
typedef long          blksize_t;
typedef long          blkcnt_t;
typedef long          suseconds_t;
typedef unsigned int  useconds_t;

#ifndef __time_t_defined
typedef __INT64_TYPE__ time_t;
#define __time_t_defined 1
#endif

#ifdef __cplusplus
}
#endif
