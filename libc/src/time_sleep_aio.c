#include "file_port.h"
#include "time_port.h"

long __astia_time_nanosleep(int64_t sec, int64_t nsec) {
    uint64_t seconds = (uint64_t) sec;
    uint64_t nanoseconds;

    if (seconds > UINT64_MAX / 1000000000ull)
        nanoseconds = UINT64_MAX;
    else {
        nanoseconds = seconds * 1000000000ull;
        if (UINT64_MAX - nanoseconds < (uint64_t) nsec)
            nanoseconds = UINT64_MAX;
        else
            nanoseconds += (uint64_t) nsec;
    }
    return __astia_fio_sleep(nanoseconds);
}
