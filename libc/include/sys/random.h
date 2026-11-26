#pragma once

/*
 * astia libc — <sys/random.h>
 *
 * Kernel-backed random bytes.  The kernel CSPRNG is shared by every
 * process and seeded from platform entropy at first use; there is no
 * blocking state once seeded and cannot fail once the kernel has entropy:
 * a failed seed is EAGAIN, never weak bytes, and there is no EINTR path.
 * GRND_NONBLOCK and GRND_RANDOM exist for source compatibility but the
 * kernel rejects nonzero flags today.  /dev/urandom is available; the
 * blocking /dev/random policy is not implemented.
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
