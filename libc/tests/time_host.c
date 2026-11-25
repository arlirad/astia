#define _POSIX_C_SOURCE 200809L

#include "../src/time_port.h"
#include <sys/syscall.h>
#include <errno.h>
#include <sys/time.h>

#include <errno.h>
#include <stddef.h>

extern long syscall(long number, ...);
extern int* __errno_location(void);
#define HOST_ERRNO (*__errno_location())
#define HOST_SYS_GETTIMEOFDAY 96

int __astia_time_host_clock_id;

long __astia_time_clock_gettime(int clock_id, int64_t* sec, int64_t* nsec) {
    struct host_timespec {
        long tv_sec;
        long tv_nsec;
    } clock_value;
    struct timeval value;

    __astia_time_host_clock_id = clock_id;
    if (clock_id == CLOCK_MONOTONIC || clock_id == CLOCK_BOOTTIME) {
        if (syscall(SYS_clock_gettime, clock_id, &clock_value) == -1)
            return -HOST_ERRNO;
        *sec  = (int64_t) clock_value.tv_sec;
        *nsec = (int64_t) clock_value.tv_nsec;
        return 0;
    }
    if (clock_id != CLOCK_REALTIME)
        return -EINVAL;
    if (syscall(HOST_SYS_GETTIMEOFDAY, &value, NULL) == -1)
        return -HOST_ERRNO;
    *sec  = (int64_t) value.tv_sec;
    *nsec = (int64_t) value.tv_usec * 1000;
    return 0;
}

/* Mirrors the kernel contract: every ID clock_gettime accepts has a
 * resolution, CLOCK_BOOTTIME included. */
long __astia_time_clock_getres(int clock_id, int64_t* sec, int64_t* nsec) {
    __astia_time_host_clock_id = clock_id;
    if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC &&
        clock_id != CLOCK_BOOTTIME)
        return -EINVAL;
    *sec  = 0;
    *nsec = 1000000;
    return 0;
}
