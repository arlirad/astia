#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __time_t_defined
typedef int64_t time_t;
#define __time_t_defined 1
#endif
#ifndef __clock_t_defined
typedef long clock_t;
#define __clock_t_defined 1
#endif

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC 1
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
#endif

#ifndef __struct_tm_defined
#define __struct_tm_defined 1
struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};
#endif

#define CLOCKS_PER_SEC 1000000L
#define TIME_UTC 1
#define CLOCK_MONOTONIC 0
#define CLOCK_REALTIME 1
#define CLOCK_BOOTTIME 2

int clock_gettime(int clock_id, struct timespec* result);
time_t time(time_t* result);
int timespec_get(struct timespec* result, int base);
struct tm* gmtime(const time_t* timer);
struct tm* gmtime_r(const time_t* timer, struct tm* result);
struct tm* localtime(const time_t* timer);
struct tm* localtime_r(const time_t* timer, struct tm* result);
time_t mktime(struct tm* value);
char* asctime(const struct tm* value);
char* ctime(const time_t* timer);
size_t strftime(char* dst, size_t cap, const char* format,
                const struct tm* value);

#ifdef __cplusplus
} /* extern "C" */
#endif
