#pragma once

#include <time.h>

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval* value, struct timezone* timezone);
int settimeofday(const struct timeval* value, const struct timezone* timezone);
int utimes(const char* path, const struct timeval times[2]);

#include <sys/select.h>
