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

struct sched_param {
    int sched_priority;
};

#define SCHED_OTHER 0
#define SCHED_FIFO  1
#define SCHED_RR    2

int sched_yield(void);

#ifdef __cplusplus
}
#endif
