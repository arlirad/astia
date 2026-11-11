#pragma once

#include <stdint.h>
#include <time.h>

enum {
    FUTEX_WAIT                    = 0,
    FUTEX_WAKE                    = 1,
    FUTEX_PRIVATE_FLAG            = 128,
    FUTEX_WAIT_PRIVATE            = 128,
    FUTEX_WAKE_PRIVATE            = 129,
    FUTEX_CANCELABLE_FLAG         = 256,
    FUTEX_WAIT_CANCELABLE_PRIVATE = FUTEX_WAIT_PRIVATE | FUTEX_CANCELABLE_FLAG,
};

#ifdef __cplusplus
extern "C" {
#endif

/* Small private-futex surface used by libc synchronization code.  A
 * wait returns immediately with EAGAIN when the word no longer equals
 * expected; wake returns the number of waiters released. */
int futex_wait(uint32_t* address, uint32_t expected);
int futex_wait_until(uint32_t* address, uint32_t expected,
                     const struct timespec* deadline);
int futex_wait_cancelable(uint32_t* address, uint32_t expected);
int futex_wait_cancelable_until(uint32_t* address, uint32_t expected,
                                const struct timespec* deadline);
int futex_wake(uint32_t* address, uint32_t count);

#ifdef __cplusplus
}
#endif
