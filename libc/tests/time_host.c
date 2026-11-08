#define _POSIX_C_SOURCE 200809L

#include "../src/time_port.h"
#include <sys/time.h>

#include <stddef.h>

extern long syscall(long number, ...);
extern int* __errno_location(void);
#define HOST_ERRNO (*__errno_location())
#define HOST_SYS_GETTIMEOFDAY 96

int __astia_time_host_clock_id;

long __astia_time_clock_gettime(int clock_id, int64_t* sec, int64_t* nsec) {
    struct timeval value;

    __astia_time_host_clock_id = clock_id;
    if (syscall(HOST_SYS_GETTIMEOFDAY, &value, NULL) == -1)
        return -HOST_ERRNO;
    *sec  = (int64_t) value.tv_sec;
    *nsec = (int64_t) value.tv_usec * 1000;
    return 0;
}
