#include "time_port.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* Days from 1970-01-01.  The era decomposition keeps negative timestamps
 * correct without relying on implementation-defined division rounding. */
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    int      y   = year - (month <= 2);
    int      era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned) (y - era * 400);
    unsigned doy = (153 * (month + (month > 2 ? (unsigned) -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t) era * 146097 + (int64_t) doe - 719468;
}

static void civil_from_days(int64_t days, int* year, unsigned* month, unsigned* day) {
    int64_t  z   = days + 719468;
    int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned) (z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int      y   = (int) yoe + (int) era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp  = (5 * doy + 2) / 153;

    *month = mp < 10 ? mp + 3 : mp - 9;
    y += *month <= 2;
    *year = y;
    *day  = doy - (153 * mp + 2) / 5 + 1;
}

int clock_gettime(int clock_id, struct timespec* result) {
    int64_t sec;
    int64_t nsec;
    long    rc;

    if (result == NULL) {
        errno = EINVAL;
        return -1;
    }
    rc = __astia_time_clock_gettime(clock_id, &sec, &nsec);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    result->tv_sec  = sec;
    result->tv_nsec = (long) nsec;
    return 0;
}

int clock_getres(int clock_id, struct timespec* result) {
    int64_t sec;
    int64_t nsec;
    long    rc;

    rc = __astia_time_clock_getres(clock_id, &sec, &nsec);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    if (result != NULL) {
        result->tv_sec  = sec;
        result->tv_nsec = (long) nsec;
    }
    return 0;
}

int clock_getcpuclockid(pid_t process_id, clockid_t* clock_id) {
    (void) process_id;
    if (clock_id == NULL)
        return EINVAL;
    return ENOSYS;
}

int gettimeofday(struct timeval* value, struct timezone* timezone) {
    struct timespec now;
    (void) timezone;
    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (clock_gettime(CLOCK_REALTIME, &now) < 0)
        return -1;
    value->tv_sec  = (long) now.tv_sec;
    value->tv_usec = now.tv_nsec / 1000;
    return 0;
}

int nanosleep(const struct timespec* requested, struct timespec* remaining) {
    int64_t remaining_sec  = 0;
    int64_t remaining_nsec = 0;
    long    rc;

    if (requested == NULL || requested->tv_sec < 0 || requested->tv_nsec < 0 ||
        requested->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    rc = __astia_time_nanosleep(requested->tv_sec, requested->tv_nsec,
                                remaining == NULL ? NULL : &remaining_sec,
                                remaining == NULL ? NULL : &remaining_nsec);
    if (rc < 0) {
        errno = (int) -rc;
        if (remaining != NULL && errno == EINTR) {
            remaining->tv_sec  = (time_t) remaining_sec;
            remaining->tv_nsec = (long) remaining_nsec;
        }
        return -1;
    }
    if (remaining != NULL) {
        remaining->tv_sec  = 0;
        remaining->tv_nsec = 0;
    }
    return 0;
}

time_t time(time_t* result) {
    struct timespec now;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return (time_t) -1;
    if (result != NULL)
        *result = now.tv_sec;
    return now.tv_sec;
}

int timespec_get(struct timespec* result, int base) {
    return base == TIME_UTC && clock_gettime(CLOCK_REALTIME, result) == 0 ? base : 0;
}

struct tm* gmtime_r(const time_t* timer, struct tm* result) {
    int64_t  days;
    int64_t  seconds;
    int      year;
    unsigned month;
    unsigned day;

    if (timer == NULL || result == NULL)
        return NULL;
    days    = *timer / 86400;
    seconds = *timer % 86400;
    if (seconds < 0) {
        seconds += 86400;
        days--;
    }
    civil_from_days(days, &year, &month, &day);
    result->tm_sec  = (int) (seconds % 60);
    result->tm_min  = (int) ((seconds / 60) % 60);
    result->tm_hour = (int) (seconds / 3600);
    result->tm_mday = (int) day;
    result->tm_mon  = (int) month - 1;
    result->tm_year = year - 1900;
    result->tm_wday = (int) ((days + 4) % 7);
    if (result->tm_wday < 0)
        result->tm_wday += 7;
    result->tm_yday =
        (int) (days_from_civil(year, month, day) - days_from_civil(year, 1, 1));
    result->tm_isdst = 0;
    return result;
}

struct tm* localtime_r(const time_t* timer, struct tm* result) {
    return gmtime_r(timer, result); /* timezone database is not present */
}

struct tm* gmtime(const time_t* timer) {
    static struct tm result;
    return gmtime_r(timer, &result);
}

struct tm* localtime(const time_t* timer) {
    static struct tm result;
    return localtime_r(timer, &result);
}

time_t mktime(struct tm* value) {
    int     year;
    int     month;
    int64_t days;
    time_t  result;

    if (value == NULL) {
        errno = EINVAL;
        return (time_t) -1;
    }
    year  = value->tm_year + 1900;
    month = value->tm_mon;
    while (month < 0) {
        month += 12;
        year--;
    }
    while (month >= 12) {
        month -= 12;
        year++;
    }
    days   = days_from_civil(year, (unsigned) month + 1, 1) + value->tm_mday - 1;
    result = days * 86400 + value->tm_hour * 3600 + value->tm_min * 60 + value->tm_sec;
    (void) gmtime_r(&result, value);
    return result;
}

char* asctime(const struct tm* value) {
    static char result[26];

    if (value == NULL || value->tm_wday < 0 || value->tm_wday > 6 || value->tm_mon < 0 ||
        value->tm_mon > 11)
        return NULL;
    return strftime(result, sizeof result, "%a %b %d %H:%M:%S %Y\n", value) == 0 ? NULL
                                                                                 : result;
}

char* ctime(const time_t* timer) {
    return asctime(localtime(timer));
}

static size_t append_text(char* dst, size_t cap, size_t at, const char* text) {
    size_t i;
    for (i = 0; text[i] != '\0'; i++)
        if (at + i + 1 < cap)
            dst[at + i] = text[i];
    return at + i;
}

size_t strftime(char* dst, size_t cap, const char* format, const struct tm* value) {
    static const char* const weekdays[]      = {"Sun", "Mon", "Tue", "Wed",
                                                "Thu", "Fri", "Sat"};
    static const char* const weekdays_long[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const months[]      = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char* const months_long[] = {
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December"};
    size_t at = 0;
    size_t i;

    if (dst == NULL || value == NULL || cap == 0 || format == NULL)
        return 0;
    if (value->tm_wday < 0 || value->tm_wday > 6 || value->tm_mon < 0 ||
        value->tm_mon > 11)
        return 0;
    for (i = 0; format[i] != '\0'; i++) {
        char text[32];
        if (format[i] != '%') {
            if (at + 1 < cap)
                dst[at] = format[i];
            at++;
            continue;
        }
        i++;
        if (format[i] == '\0')
            break;
        text[0] = '\0';
        switch (format[i]) {
        case 'a':
            at = append_text(dst, cap, at, weekdays[value->tm_wday]);
            break;
        case 'A':
            at = append_text(dst, cap, at, weekdays_long[value->tm_wday]);
            break;
        case 'b':
        case 'h':
            at = append_text(dst, cap, at, months[value->tm_mon]);
            break;
        case 'B':
            at = append_text(dst, cap, at, months_long[value->tm_mon]);
            break;
        case 'Y':
            (void) snprintf(text, sizeof text, "%04d", value->tm_year + 1900);
            at = append_text(dst, cap, at, text);
            break;
        case 'm':
            (void) snprintf(text, sizeof text, "%02d", value->tm_mon + 1);
            at = append_text(dst, cap, at, text);
            break;
        case 'd':
            (void) snprintf(text, sizeof text, "%02d", value->tm_mday);
            at = append_text(dst, cap, at, text);
            break;
        case 'H':
            (void) snprintf(text, sizeof text, "%02d", value->tm_hour);
            at = append_text(dst, cap, at, text);
            break;
        case 'M':
            (void) snprintf(text, sizeof text, "%02d", value->tm_min);
            at = append_text(dst, cap, at, text);
            break;
        case 'S':
            (void) snprintf(text, sizeof text, "%02d", value->tm_sec);
            at = append_text(dst, cap, at, text);
            break;
        case 'j':
            (void) snprintf(text, sizeof text, "%03d", value->tm_yday + 1);
            at = append_text(dst, cap, at, text);
            break;
        case 'w':
            (void) snprintf(text, sizeof text, "%d", value->tm_wday);
            at = append_text(dst, cap, at, text);
            break;
        case 'n':
            at = append_text(dst, cap, at, "\n");
            break;
        case 't':
            at = append_text(dst, cap, at, "\t");
            break;
        case '%':
            at = append_text(dst, cap, at, "%");
            break;
        default:
            return 0;
        }
    }
    if (at >= cap)
        return 0;
    dst[at] = '\0';
    return at;
}
