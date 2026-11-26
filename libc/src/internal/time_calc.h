#pragma once

#include <stdint.h>

/* Days from 1970-01-01.  The era decomposition keeps negative timestamps
 * correct without relying on implementation-defined division rounding. */
static inline int64_t __astia_time_days_from_civil(int year, unsigned month,
                                                   unsigned day) {
    int      y   = year - (month <= 2);
    int      era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned) (y - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? (unsigned) -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return (int64_t) era * 146097 + (int64_t) doe - 719468;
}

static inline int __astia_time_is_leap_year(int64_t year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static inline int __astia_time_weekday_from_days(int64_t days) {
    return (int) (((days + 4) % 7 + 7) % 7);
}
