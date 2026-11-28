#pragma once

/*
 * astia libc — <sys/random.h>
 *
 * Kernel-backed random bytes.  The kernel CSPRNG is shared by every
 * process and seeded from platform entropy at first use.  Until a usable
 * entropy source is available the call fails with EAGAIN; a failed seed
 * never yields weak bytes.  Once seeded the pool does not block, so there
 * is no EINTR path.  GRND_NONBLOCK and GRND_RANDOM exist for source
 * compatibility, but the kernel rejects every nonzero flags value with
 * EINVAL today.  Requests longer than 1 MiB are also rejected with EINVAL.
 * /dev/urandom is available; the blocking /dev/random policy is not
 * implemented.
 *
 * A successful call returns the number of bytes written.  That count may
 * be positive but shorter than length when a failure occurs after part of
 * the request has been copied (a later copy can fail even though the
 * buffer validated).  Callers that need exactly length bytes must check
 * the result and retry the remainder.  On failure the call returns -1 and
 * sets errno.
 */

#include <sys/types.h>

#include <stddef.h>

#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK 0x0001
#endif
#ifndef GRND_RANDOM
#define GRND_RANDOM 0x0002
#endif

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getrandom(void* buffer, size_t length, unsigned flags);

#ifdef __cplusplus
}
#endif
