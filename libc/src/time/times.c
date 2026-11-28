#include <sys/time.h>

#include <time.h>

clock_t times(struct tms* result) {
    if (result != 0) {
        result->tms_utime  = 0;
        result->tms_stime  = 0;
        result->tms_cutime = 0;
        result->tms_cstime = 0;
    }

    return 0;
}

clock_t clock(void) {
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) < 0)
        return (clock_t) -1;

    return (clock_t) value.tv_sec * CLOCKS_PER_SEC +
           (clock_t) (value.tv_nsec / (1000000000L / CLOCKS_PER_SEC));
}
