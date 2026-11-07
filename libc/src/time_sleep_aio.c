#include "file_port.h"
#include "time_port.h"

long __astia_time_nanosleep(int64_t sec, int64_t nsec, int64_t* remaining_sec,
                            int64_t* remaining_nsec) {
    uint64_t seconds = (uint64_t) sec;
    uint64_t nanoseconds;
    uint64_t remaining = 0;
    long     rc;

    if (seconds > UINT64_MAX / 1000000000ull)
        nanoseconds = UINT64_MAX;
    else {
        nanoseconds = seconds * 1000000000ull;
        if (UINT64_MAX - nanoseconds < (uint64_t) nsec)
            nanoseconds = UINT64_MAX;
        else
            nanoseconds += (uint64_t) nsec;
    }
    rc = __astia_fio_sleep(nanoseconds, &remaining);
    if (rc == -EINTR) {
        if (remaining_sec != NULL)
            *remaining_sec = (int64_t) (remaining / 1000000000ull);
        if (remaining_nsec != NULL)
            *remaining_nsec = (int64_t) (remaining % 1000000000ull);
    }
    return rc;
}
