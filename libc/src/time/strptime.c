#include <time.h>

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int parse_number(const char** input, int max_digits, int* value) {
    const char* at = *input;

    int digits = 0;
    int result = 0;

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

char* strptime(const char* input, const char* format, struct tm* value) {
    const char* at = input;

    if (input == NULL || format == NULL || value == NULL)
        return NULL;

    while (*format != '\0') {
        int number;

        if (is_space(*format)) {
            while (is_space(*format)) {
                format++;
            }

            while (is_space(*at)) {
                at++;
            }

            continue;
        }

        if (*format != '%') {
            if (*at != *format)
                return NULL;

            at++;
            format++;
            continue;
        }

        format++;

        switch (*format) {
        case 'Y':
            if (!parse_number(&at, 4, &number))
                return NULL;

            value->tm_year = number - 1900;

            break;
        case 'm':
            if (!parse_number(&at, 2, &number) || number < 1 || number > 12)
                return NULL;

            value->tm_mon = number - 1;

            break;
        case 'd':
            if (!parse_number(&at, 2, &number) || number < 1 || number > 31)
                return NULL;

            value->tm_mday = number;

            break;
        case 'H':
            if (!parse_number(&at, 2, &number) || number < 0 || number > 23)
                return NULL;

            value->tm_hour = number;

            break;
        case 'M':
            if (!parse_number(&at, 2, &number) || number < 0 || number > 59)
                return NULL;

            value->tm_min = number;

            break;
        case 'S':
            if (!parse_number(&at, 2, &number) || number < 0 || number > 60)
                return NULL;

            value->tm_sec = number;

            break;
        case '%':
            if (*at != '%')
                return NULL;

            at++;
            break;
        case '\0':
            return NULL;
        default:
            return NULL;
        }

        format++;
    }

    return (char*) at;
}
