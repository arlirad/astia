#pragma once

#include <stdint.h>

long __astia_time_clock_gettime(int clock_id, int64_t* sec, int64_t* nsec);
long __astia_time_nanosleep(int64_t sec, int64_t nsec, int64_t* remaining_sec,
                            int64_t* remaining_nsec);
