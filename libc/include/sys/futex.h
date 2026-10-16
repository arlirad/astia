#pragma once

#include <stdint.h>

enum {
    FUTEX_WAIT_PRIVATE = 128,
    FUTEX_WAKE_PRIVATE = 129,
};

#ifdef __cplusplus
extern "C" {
#endif

/* Small private-futex surface used by libc synchronization code.  A
 * wait returns immediately with EAGAIN when the word no longer equals
 * expected; wake returns the number of waiters released. */
int futex_wait(uint32_t* address, uint32_t expected);
int futex_wake(uint32_t* address, uint32_t count);

#ifdef __cplusplus
}
#endif
