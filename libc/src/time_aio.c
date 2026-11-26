#include <astia/api/syscalls.h>

#include "time_port.h"

long __astia_time_clock_gettime(int clock_id, int64_t* sec, int64_t* nsec) {
    astia_timespec result;

    long rc = astia_clock_gettime((uint32_t) clock_id, &result);

    if (rc < 0)
        return rc;

    *sec  = result.tv_sec;
    *nsec = result.tv_nsec;

    return 0;
}

long __astia_time_clock_getres(int clock_id, int64_t* sec, int64_t* nsec) {
    astia_timespec result;

    long rc = astia_clock_getres((uint32_t) clock_id, &result);

    if (rc < 0)
        return rc;

    *sec  = result.tv_sec;
    *nsec = result.tv_nsec;

    return 0;
}
