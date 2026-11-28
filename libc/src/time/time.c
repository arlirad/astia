#include "internal/time_calc.h"
#include "internal/time_names.h"
#include "internal/time_port.h"
#include <sys/time.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

const char* const __astia_time_weekday_names[7]      = {"Sun", "Mon", "Tue", "Wed",
                                                        "Thu", "Fri", "Sat"};
const char* const __astia_time_weekday_full_names[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const __astia_time_month_names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const __astia_time_month_full_names[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};

static void civil_from_days(int64_t days, int64_t* year, unsigned* month, unsigned* day) {
    int64_t z   = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;

    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y   = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp  = (5 * doy + 2) / 153;

    *month = (unsigned) (mp < 10 ? mp + 3 : mp - 9);
    y += *month <= 2;
    *year = y;
    *day  = (unsigned) (doy - (153 * mp + 2) / 5 + 1);
}

int clock_gettime(int clock_id, struct timespec* result) {
    int64_t sec;
    int64_t nsec;

    long rc;

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

    long rc;

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

int settimeofday(const struct timeval* value, const struct timezone* timezone) {
    (void) timezone;

    if (value == NULL || value->tv_usec < 0 || value->tv_usec >= 1000000L) {
        errno = EINVAL;

        return -1;
    }

    errno = ENOSYS;

    return -1;
}

int nanosleep(const struct timespec* requested, struct timespec* remaining) {
    int64_t remaining_sec  = 0;
    int64_t remaining_nsec = 0;

    long rc;

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

int clock_nanosleep(int clock_id, int flags, const struct timespec* requested,
                    struct timespec* remaining) {
    int64_t remaining_sec  = 0;
    int64_t remaining_nsec = 0;

    long rc;

    if (requested == NULL || requested->tv_sec < 0 || requested->tv_nsec < 0 ||
        requested->tv_nsec >= 1000000000L || (flags & ~TIMER_ABSTIME) != 0)
        return EINVAL;

    rc = __astia_time_clock_nanosleep(
        clock_id, flags, requested->tv_sec, requested->tv_nsec,
        (flags & TIMER_ABSTIME) == 0 && remaining != NULL ? &remaining_sec : NULL,
        (flags & TIMER_ABSTIME) == 0 && remaining != NULL ? &remaining_nsec : NULL);
    if (rc < 0) {
        if (remaining != NULL && (flags & TIMER_ABSTIME) == 0 && rc == -EINTR) {
            remaining->tv_sec  = (time_t) remaining_sec;
            remaining->tv_nsec = (long) remaining_nsec;
        }

        return (int) -rc;
    }

    if (remaining != NULL && (flags & TIMER_ABSTIME) == 0) {
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
    int64_t days;
    int64_t seconds;

    int64_t year;

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

    if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
        errno = EOVERFLOW;

        return NULL;
    }

    result->tm_sec  = (int) (seconds % 60);
    result->tm_min  = (int) ((seconds / 60) % 60);
    result->tm_hour = (int) (seconds / 3600);
    result->tm_mday = (int) day;
    result->tm_mon  = (int) month - 1;
    result->tm_year = (int) (year - 1900);
    result->tm_wday = (int) ((days + 4) % 7);

    if (result->tm_wday < 0)
        result->tm_wday += 7;

    result->tm_yday  = (int) (__astia_time_days_from_civil(year, month, day) -
                             __astia_time_days_from_civil(year, 1, 1));
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
    int64_t year;
    int64_t month;

    int64_t days;
    int64_t seconds;
    int64_t remainder;
    int64_t normalized;

    time_t result;

    if (value == NULL) {
        errno = EINVAL;

        return (time_t) -1;
    }

    year  = (int64_t) value->tm_year + 1900;
    month = value->tm_mon;
    year += month / 12;
    month %= 12;
    if (month < 0) {
        month += 12;

        year--;
    }

    if (year < (int64_t) INT_MIN + 1900 || year > INT_MAX) {
        errno = EOVERFLOW;

        return (time_t) -1;
    }

    days = __astia_time_days_from_civil(year, (unsigned) month + 1, 1) +
           (int64_t) value->tm_mday - 1;

    if (days > INT64_MAX / 86400 || days < INT64_MIN / 86400) {
        errno = EOVERFLOW;

        return (time_t) -1;
    }

    normalized = days * 86400;
    remainder =
        (int64_t) value->tm_hour * 3600 + (int64_t) value->tm_min * 60 + value->tm_sec;

    if ((remainder > 0 && normalized > INT64_MAX - remainder) ||
        (remainder < 0 && normalized < INT64_MIN - remainder)) {
        errno = EOVERFLOW;

        return (time_t) -1;
    }

    seconds = normalized + remainder;
    result  = (time_t) seconds;

    if ((int64_t) result != seconds) {
        errno = EOVERFLOW;

        return (time_t) -1;
    }

    if (gmtime_r(&result, value) == NULL)
        return (time_t) -1;

    return result;
}

char* asctime(const struct tm* value) {
    static char result[26];

    if (value == NULL || value->tm_wday < 0 || value->tm_wday > 6 || value->tm_mon < 0 ||
        value->tm_mon > 11)
        return NULL;

    return strftime(result, sizeof result, "%a %b %e %H:%M:%S %Y\n", value) == 0 ? NULL
                                                                                 : result;
}

char* ctime(const time_t* timer) {
    return asctime(localtime(timer));
}

static size_t append_text(char* dst, size_t cap, size_t at, const char* text) {
    size_t i;

    for (i = 0; text[i] != '\0'; i++) {
        if (at + i + 1 < cap)
            dst[at + i] = text[i];
    }

    return at + i;
}

static size_t append_number(char* dst, size_t cap, size_t at, const char* format,
                            long long number) {
    char text[32];

    (void) snprintf(text, sizeof text, format, number);

    return append_text(dst, cap, at, text);
}

static size_t append_embedded(char* dst, size_t cap, size_t at, const char* format,
                              const struct tm* value) {
    char   text[64];
    size_t length = strftime(text, sizeof text, format, value);

    if (length == 0 || length >= sizeof text)
        return SIZE_MAX;

    return append_text(dst, cap, at, text);
}

static int64_t iso_weeks_in_year(int64_t year, int jan1_weekday) {
    if (jan1_weekday == 4 || (jan1_weekday == 3 && __astia_time_is_leap_year(year)))
        return 53;

    return 52;
}

static int64_t century_of(int64_t year) {
    return year >= 0 ? year / 100 : -((99 - year) / 100);
}

static int alternative_format_allowed(char modifier, char conversion) {
    if (modifier == 'E') {
        switch (conversion) {
        case 'C':
        case 'c':
        case 'X':
        case 'x':
        case 'Y':
        case 'y':
            return 1;
        default:
            return 0;
        }
    }

    switch (conversion) {
    case 'H':
    case 'I':
    case 'M':
    case 'S':
    case 'U':
    case 'V':
    case 'W':
    case 'd':
    case 'e':
    case 'm':
    case 'u':
    case 'w':
    case 'y':
        return 1;
    default:
        return 0;
    }
}

size_t strftime(char* dst, size_t cap, const char* format, const struct tm* value) {
    int64_t year;
    int64_t iso_wday;
    int64_t jan1_wday;
    int64_t iso_year;
    int64_t week;
    int64_t hour;
    int64_t hour12;

    size_t at = 0;
    size_t i;

    if (dst == NULL || value == NULL || cap == 0 || format == NULL)
        return 0;

    if (value->tm_wday < 0 || value->tm_wday > 6 || value->tm_mon < 0 ||
        value->tm_mon > 11)
        return 0;

    year      = (int64_t) value->tm_year + 1900;
    iso_wday  = value->tm_wday == 0 ? 7 : value->tm_wday;
    jan1_wday = ((iso_wday - 1 - (int64_t) value->tm_yday) % 7 + 7) % 7 + 1;
    iso_year  = year;
    week      = ((int64_t) value->tm_yday - iso_wday + 11) / 7;
    if (week < 1) {
        int64_t previous_length = __astia_time_is_leap_year(year - 1) ? 366 : 365;
        int64_t previous_jan1   = ((jan1_wday - 1 - previous_length) % 7 + 7) % 7 + 1;

        iso_year = year - 1;
        week += iso_weeks_in_year(iso_year, (int) previous_jan1);
    }
    else if (week > iso_weeks_in_year(year, (int) jan1_wday)) {
        iso_year = year + 1;
        week     = 1;
    }

    hour   = value->tm_hour;
    hour12 = hour % 12;
    if (hour12 == 0)
        hour12 = 12;

    for (i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            if (at + 1 < cap)
                dst[at] = format[i];

            at++;
            continue;
        }

        i++;

        if (format[i] == '\0')
            break;

        if (format[i] == 'E' || format[i] == 'O') {
            char modifier = format[i];

            i++;

            if (!alternative_format_allowed(modifier, format[i]))
                return 0;
        }

        switch (format[i]) {
        case 'a':
            at = append_text(dst, cap, at, __astia_time_weekday_names[value->tm_wday]);

            break;
        case 'A':
            at = append_text(dst, cap, at,
                             __astia_time_weekday_full_names[value->tm_wday]);

            break;
        case 'b':
        case 'h':
            at = append_text(dst, cap, at, __astia_time_month_names[value->tm_mon]);

            break;
        case 'B':
            at = append_text(dst, cap, at, __astia_time_month_full_names[value->tm_mon]);

            break;
        case 'C':
            at = append_number(dst, cap, at, "%02lld", (long long) century_of(year));

            break;
        case 'c':
            at = append_embedded(dst, cap, at, "%a %b %e %H:%M:%S %Y", value);

            break;
        case 'd':
            at = append_number(dst, cap, at, "%02lld", (long long) value->tm_mday);

            break;
        case 'D':
        case 'x':
            at = append_embedded(dst, cap, at, "%m/%d/%y", value);

            break;
        case 'e':
            at = append_number(dst, cap, at, "%2lld", (long long) value->tm_mday);

            break;
        case 'F':
            at = append_embedded(dst, cap, at, "%Y-%m-%d", value);

            break;
        case 'g':
            at = append_number(dst, cap, at, "%02lld",
                               (long long) ((iso_year % 100 + 100) % 100));

            break;
        case 'G':
            at = append_number(dst, cap, at, "%04lld", (long long) iso_year);

            break;
        case 'H':
            at = append_number(dst, cap, at, "%02lld", (long long) hour);

            break;
        case 'I':
            at = append_number(dst, cap, at, "%02lld", (long long) hour12);

            break;
        case 'j':
            at = append_number(dst, cap, at, "%03lld", (long long) value->tm_yday + 1);

            break;
        case 'k':
            at = append_number(dst, cap, at, "%2lld", (long long) hour);

            break;
        case 'l':
            at = append_number(dst, cap, at, "%2lld", (long long) hour12);

            break;
        case 'm':
            at = append_number(dst, cap, at, "%02lld", (long long) value->tm_mon + 1);

            break;
        case 'M':
            at = append_number(dst, cap, at, "%02lld", (long long) value->tm_min);

            break;
        case 'n':
            at = append_text(dst, cap, at, "\n");

            break;
        case 'p':
            at = append_text(dst, cap, at, value->tm_hour < 12 ? "AM" : "PM");

            break;
        case 'P':
            at = append_text(dst, cap, at, value->tm_hour < 12 ? "am" : "pm");

            break;
        case 'r':
            at = append_embedded(dst, cap, at, "%I:%M:%S %p", value);

            break;
        case 'R':
            at = append_embedded(dst, cap, at, "%H:%M", value);

            break;
        case 's': {
            struct tm utc = *value;

            if (value->tm_year > INT_MAX - 1900)
                return 0;

            at = append_number(dst, cap, at, "%lld", (long long) mktime(&utc));

            break;
        }
        case 'S':
            at = append_number(dst, cap, at, "%02lld", (long long) value->tm_sec);

            break;
        case 't':
            at = append_text(dst, cap, at, "\t");

            break;
        case 'T':
        case 'X':
            at = append_embedded(dst, cap, at, "%H:%M:%S", value);

            break;
        case 'u':
            at = append_number(dst, cap, at, "%lld", (long long) iso_wday);

            break;
        case 'U':
            at = append_number(dst, cap, at, "%02lld",
                               ((long long) value->tm_yday + 7 - value->tm_wday) / 7);

            break;
        case 'V':
            at = append_number(dst, cap, at, "%02lld", (long long) week);

            break;
        case 'w':
            at = append_number(dst, cap, at, "%lld", (long long) value->tm_wday);

            break;
        case 'W':
            at = append_number(
                dst, cap, at, "%02lld",
                ((long long) value->tm_yday + 7 - (value->tm_wday + 6) % 7) / 7);

            break;
        case 'y':
            at = append_number(dst, cap, at, "%02lld",
                               (long long) ((year % 100 + 100) % 100));

            break;
        case 'Y':
            at = append_number(dst, cap, at, "%04lld", (long long) year);

            break;
        case '%':
            at = append_text(dst, cap, at, "%");

            break;
        default:
            return 0;
        }

        if (at == SIZE_MAX)
            return 0;
    }

    if (at >= cap)
        return 0;

    dst[at] = '\0';

    return at;
}

void tzset(void) {}
