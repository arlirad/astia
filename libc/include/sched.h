#pragma once

/*
 * astia libc — <sched.h>
 *
 * The one cooperative scheduling primitive.  Returns 0 on success,
 * -1 with errno otherwise.
 */

#ifdef __cplusplus
extern "C" {
#endif

int sched_yield(void);

#ifdef __cplusplus
}
#endif
