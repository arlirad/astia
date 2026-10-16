#define _POSIX_C_SOURCE 200809L

#include "../src/time_port.h"
#include <sys/time.h>

#include <stddef.h>

long __astia_time_clock_gettime(int clock_id, int64_t* sec, int64_t* nsec) {
    struct timeval value;

    (void) clock_id; /* gettimeofday is the host test's realtime source */
    if (gettimeofday(&value, NULL) != 0)
        return -1;
    *sec  = (int64_t) value.tv_sec;
    *nsec = (int64_t) value.tv_usec * 1000;
    return 0;
}
