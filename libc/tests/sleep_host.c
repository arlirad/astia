#include "../src/time_port.h"
#include <errno.h>
#include <stddef.h>

int     sleep_host_calls;
int64_t sleep_host_sec;
int64_t sleep_host_nsec;
long    sleep_host_result;
int64_t sleep_host_remaining_sec;
int64_t sleep_host_remaining_nsec;

long __astia_time_nanosleep(int64_t sec, int64_t nsec, int64_t* remaining_sec,
                            int64_t* remaining_nsec) {
    ++sleep_host_calls;
    sleep_host_sec  = sec;
    sleep_host_nsec = nsec;
    if (sleep_host_result == -EINTR) {
        if (remaining_sec != NULL)
            *remaining_sec = sleep_host_remaining_sec;
        if (remaining_nsec != NULL)
            *remaining_nsec = sleep_host_remaining_nsec;
    }
    return sleep_host_result;
}
