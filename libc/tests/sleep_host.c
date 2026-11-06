#include "../src/time_port.h"

int     sleep_host_calls;
int64_t sleep_host_sec;
int64_t sleep_host_nsec;

long __astia_time_nanosleep(int64_t sec, int64_t nsec) {
    ++sleep_host_calls;
    sleep_host_sec  = sec;
    sleep_host_nsec = nsec;
    return 0;
}
