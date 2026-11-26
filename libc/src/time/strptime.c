#include "internal/time_calc.h"
#include "internal/time_names.h"

#include <limits.h>
#include <string.h>
#include <time.h>

struct parse_state {
    int have_century;
    int century;
    int have_two_digit_year;
    int two_digit_year;
    int have_hour12;
    int hour12;
    int have_ampm;
    int is_pm;
    int have_wday;
    int have_yday;
    int have_week_u;
    int week_u;
    int have_week_w;
    int week_w;
    int have_week_v;
    int week_v;
    int have_iso_year;
    int iso_year;
    int have_iso_two_digit_year;
    int iso_two_digit_year;
};

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char) (c + ('a' - 'A')) : c;
}

static int match_ci(const char** input, const char* text) {
    const char* at = *input;

    size_t i;

    for (i = 0; text[i] != '\0'; i++) {
        if (lower(at[i]) != lower(text[i]))
            return 0;
    }

    *input = at + i;

    return 1;
}

static int match_name(const char** input, const char* short_name, const char* long_name) {
    const char* at = *input;

    if (match_ci(&at, long_name)) {
        *input = at;

        return 1;
    }

    if (!match_ci(&at, short_name))
        return 0;

    *input = at;

    return 1;
}

static int parse_digits(const char** input, int max_digits, long long* value) {
    const char* at = *input;

    int       digits = 0;
    long long result = 0;

    while (digits < max_digits && is_digit(at[digits])) {
        result = result * 10 + (at[digits] - '0');

        digits++;
    }

    if (digits == 0)
        return 0;

    *input = at + digits;
    *value = result;

    return 1;
}

static int parse_signed_digits(const char** input, int max_digits, long long* value) {
    const char* at = *input;

    int       negative = 0;
    long long magnitude;

    if (*at == '+' || *at == '-') {
        negative = *at == '-';

        at++;
    }

    if (!parse_digits(&at, max_digits, &magnitude))
        return 0;

    *input = at;
    *value = negative ? -magnitude : magnitude;

    return 1;
}

static int parse_range(const char** input, int max_digits, int minimum, int maximum,
                       int* value) {
    long long number;

    if (!parse_digits(input, max_digits, &number) || number < minimum || number > maximum)
        return 0;

    *value = (int) number;

    return 1;
}

static int parse_blank_padded(const char** input, int max_digits, int minimum,
                              int maximum, int* value) {
    const char* at = *input;

    while (*at == ' ') {
        at++;
    }

    if (!parse_range(&at, max_digits, minimum, maximum, value))
        return 0;

    *input = at;

    return 1;
}

static int match_weekday(const char** input, int* weekday) {
    int index;

    for (index = 0; index < 7; index++) {
        const char* at = *input;

        if (!match_name(&at, __astia_time_weekday_names[index],
                        __astia_time_weekday_full_names[index]))
            continue;

        *input   = at;
        *weekday = index;

        return 1;
    }

    return 0;
}

static int match_month(const char** input, int* month) {
    int index;

    for (index = 0; index < 12; index++) {
        const char* at = *input;

        if (!match_name(&at, __astia_time_month_names[index],
                        __astia_time_month_full_names[index]))
            continue;

        *input = at;
        *month = index;

        return 1;
    }

    return 0;
}

static int match_day_period(const char** input, int* is_pm) {
    const char* at = *input;

    if (match_ci(&at, "AM")) {
        *input = at;
        *is_pm = 0;

        return 1;
    }

    if (!match_ci(&at, "PM"))
        return 0;

    *input = at;
    *is_pm = 1;

    return 1;
}

static int iso_weekday(const struct tm* value) {
    return value->tm_wday == 0 ? 7 : value->tm_wday;
}

static int week_yday_sunday(int year, int week, int weekday) {
    int jan1 = __astia_time_weekday_from_days(__astia_time_days_from_civil(year, 1, 1));

    if (week == 0)
        return weekday - jan1;

    return (7 - jan1) % 7 + (week - 1) * 7 + weekday;
}

static int week_yday_monday(int year, int week, int weekday) {
    int jan1 =
        (__astia_time_weekday_from_days(__astia_time_days_from_civil(year, 1, 1)) + 6) %
        7;
    int monday = (weekday + 6) % 7;

    if (week == 0)
        return monday - jan1;

    return (7 - jan1) % 7 + (week - 1) * 7 + monday;
}

static int iso_week_yday(int year, int week, int weekday) {
    int64_t jan4 = __astia_time_days_from_civil(year, 1, 4);

    int jan4_wday = __astia_time_weekday_from_days(jan4);
    int jan4_iso  = jan4_wday == 0 ? 7 : jan4_wday;

    int64_t monday = jan4 - (jan4_iso - 1);
    int64_t target = monday + (week - 1) * 7 + (weekday - 1);

    return (int) (target - __astia_time_days_from_civil(year, 1, 1));
}

static void resolve_year(struct parse_state* state, struct tm* value) {
    if (state->have_two_digit_year) {
        int century = state->have_century ? state->century
                                          : (state->two_digit_year >= 69 ? 19 : 20);

        value->tm_year = century * 100 + state->two_digit_year - 1900;

        return;
    }

    if (!state->have_century)
        return;

    value->tm_year = state->century * 100 - 1900;
}

static void resolve_week(struct parse_state* state, struct tm* value) {
    int year = value->tm_year + 1900;

    if (state->have_yday || !state->have_wday)
        return;

    if (state->have_week_v) {
        int iso_year;

        if (state->have_iso_year)
            iso_year = state->iso_year;
        else if (state->have_iso_two_digit_year)
            iso_year = (state->iso_two_digit_year >= 69 ? 1900 : 2000) +
                       state->iso_two_digit_year;
        else
            return;

        value->tm_year = iso_year - 1900;
        value->tm_yday = iso_week_yday(iso_year, state->week_v, iso_weekday(value));

        return;
    }

    if (state->have_week_u) {
        value->tm_yday = week_yday_sunday(year, state->week_u, value->tm_wday);

        return;
    }

    if (state->have_week_w)
        value->tm_yday = week_yday_monday(year, state->week_w, value->tm_wday);
}

static void resolve_state(struct parse_state* state, struct tm* value) {
    resolve_year(state, value);

    if (state->have_hour12) {
        value->tm_hour = state->hour12 % 12;

        if (state->have_ampm && state->is_pm)
            value->tm_hour += 12;
    }

    resolve_week(state, value);
}

static int parse_format(const char** at, const char* format, struct tm* value,
                        struct parse_state* state);

static int parse_format(const char** at, const char* format, struct tm* value,
                        struct parse_state* state) {
    while (*format != '\0') {
        int number;

        if (is_space(*format)) {
            while (is_space(*format)) {
                format++;
            }

            while (is_space(**at)) {
                (*at)++;
            }

            continue;
        }

        if (*format != '%') {
            if (**at != *format)
                return 0;

            (*at)++;
            format++;

            continue;
        }

        format++;

        switch (*format) {
        case 'a':
        case 'A':
            if (!match_weekday(at, &number))
                return 0;

            value->tm_wday   = number;
            state->have_wday = 1;

            break;
        case 'b':
        case 'B':
        case 'h':
            if (!match_month(at, &number))
                return 0;

            value->tm_mon = number;

            break;
        case 'c':
            if (!parse_format(at, "%a %b %e %H:%M:%S %Y", value, state))
                return 0;

            break;
        case 'C':
            if (!parse_range(at, 2, 0, 99, &number))
                return 0;

            state->century      = number;
            state->have_century = 1;

            break;
        case 'd':
            if (!parse_range(at, 2, 1, 31, &number))
                return 0;

            value->tm_mday = number;

            break;
        case 'D':
        case 'x':
            if (!parse_format(at, "%m/%d/%y", value, state))
                return 0;

            break;
        case 'e':
            if (!parse_blank_padded(at, 2, 1, 31, &number))
                return 0;

            value->tm_mday = number;

            break;
        case 'F':
            if (!parse_format(at, "%Y-%m-%d", value, state))
                return 0;

            break;
        case 'g':
            if (!parse_range(at, 2, 0, 99, &number))
                return 0;

            state->iso_two_digit_year      = number;
            state->have_iso_two_digit_year = 1;

            break;
        case 'G': {
            long long year;

            if (!parse_signed_digits(at, 4, &year) || year - 1900 < INT_MIN ||
                year - 1900 > INT_MAX)
                return 0;

            state->iso_year      = (int) year;
            state->have_iso_year = 1;

            break;
        }
        case 'H':
            if (!parse_range(at, 2, 0, 23, &number))
                return 0;

            value->tm_hour = number;

            break;
        case 'I':
        case 'l':
            if (!parse_blank_padded(at, 2, 1, 12, &number))
                return 0;

            state->hour12      = number;
            state->have_hour12 = 1;

            break;
        case 'j':
            if (!parse_range(at, 3, 1, 366, &number))
                return 0;

            value->tm_yday   = number - 1;
            state->have_yday = 1;

            break;
        case 'k':
            if (!parse_blank_padded(at, 2, 0, 23, &number))
                return 0;

            value->tm_hour = number;

            break;
        case 'm':
            if (!parse_range(at, 2, 1, 12, &number))
                return 0;

            value->tm_mon = number - 1;

            break;
        case 'M':
            if (!parse_range(at, 2, 0, 59, &number))
                return 0;

            value->tm_min = number;

            break;
        case 'n':
        case 't':
            while (is_space(**at)) {
                (*at)++;
            }

            break;
        case 'p':
        case 'P':
            if (!match_day_period(at, &number))
                return 0;

            state->is_pm     = number;
            state->have_ampm = 1;

            break;
        case 'r':
            if (!parse_format(at, "%I:%M:%S %p", value, state))
                return 0;

            break;
        case 'R':
            if (!parse_format(at, "%H:%M", value, state))
                return 0;

            break;
        case 's': {
            long long seconds;

            time_t    timer;
            struct tm result;

            if (!parse_signed_digits(at, 18, &seconds))
                return 0;

            timer = (time_t) seconds;

            if ((long long) timer != seconds || gmtime_r(&timer, &result) == NULL)
                return 0;

            *value = result;

            break;
        }
        case 'S':
            if (!parse_range(at, 2, 0, 60, &number))
                return 0;

            value->tm_sec = number;

            break;
        case 'T':
        case 'X':
            if (!parse_format(at, "%H:%M:%S", value, state))
                return 0;

            break;
        case 'u':
            if (!parse_range(at, 1, 1, 7, &number))
                return 0;

            value->tm_wday   = number % 7;
            state->have_wday = 1;

            break;
        case 'U':
            if (!parse_range(at, 2, 0, 53, &number))
                return 0;

            state->week_u      = number;
            state->have_week_u = 1;

            break;
        case 'V':
            if (!parse_range(at, 2, 1, 53, &number))
                return 0;

            state->week_v      = number;
            state->have_week_v = 1;

            break;
        case 'w':
            if (!parse_range(at, 1, 0, 6, &number))
                return 0;

            value->tm_wday   = number;
            state->have_wday = 1;

            break;
        case 'W':
            if (!parse_range(at, 2, 0, 53, &number))
                return 0;

            state->week_w      = number;
            state->have_week_w = 1;

            break;
        case 'y':
            if (!parse_range(at, 2, 0, 99, &number))
                return 0;

            state->two_digit_year      = number;
            state->have_two_digit_year = 1;

            break;
        case 'Y': {
            long long year;

            if (!parse_signed_digits(at, 4, &year) || year - 1900 < INT_MIN ||
                year - 1900 > INT_MAX)
                return 0;

            value->tm_year = (int) (year - 1900);

            break;
        }
        case 'z': {
            const char* probe = *at;

            int hours;
            int minutes;

            if (*probe != '+' && *probe != '-')
                return 0;

            probe++;

            if (!parse_range(&probe, 2, 0, 23, &hours))
                return 0;

            if (*probe == ':')
                probe++;

            if (!parse_range(&probe, 2, 0, 59, &minutes))
                return 0;

            *at = probe;

            break;
        }
        case 'Z': {
            const char* probe = *at;

            while (is_alpha(*probe)) {
                probe++;
            }

            if (probe == *at)
                return 0;

            *at = probe;

            break;
        }
        case '%':
            if (**at != '%')
                return 0;

            (*at)++;

            break;
        case '\0':
            return 0;
        default:
            return 0;
        }

        format++;
    }

    return 1;
}

char* strptime(const char* input, const char* format, struct tm* value) {
    struct parse_state state;
    const char*        at = input;

    if (input == NULL || format == NULL || value == NULL)
        return NULL;

    memset(&state, 0, sizeof state);

    if (!parse_format(&at, format, value, &state))
        return NULL;

    resolve_state(&state, value);

    return (char*) at;
}
