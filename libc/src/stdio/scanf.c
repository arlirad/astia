#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct scan_input {
    int (*get)(void*);
    int (*unget)(int, void*);
    void*  data;
    size_t count;
    int    input_eof;
};

struct string_input {
    const unsigned char* p;
};

static int string_get(void* data) {
    struct string_input* in = data;

    if (*in->p == '\0')
        return EOF;

    return *in->p++;
}

static int string_unget(int c, void* data) {
    struct string_input* in = data;

    if (c == EOF || in->p == NULL)
        return EOF;

    in->p--;

    return 0;
}

static int file_get(void* data) {
    return fgetc((FILE*) data);
}

static int file_unget(int c, void* data) {
    return ungetc(c, (FILE*) data) == EOF ? EOF : 0;
}

static int scan_get(struct scan_input* in) {
    int c = in->get(in->data);
    if (c == EOF)
        in->input_eof = 1;
    else
        in->count++;

    return c;
}

static void scan_unget(struct scan_input* in, int c) {
    if (c != EOF && in->unget(c, in->data) == 0 && in->count != 0)
        in->count--;
}

static void skip_input_space(struct scan_input* in) {
    int c;

    do {
        c = scan_get(in);
    } while (c != EOF && isspace((unsigned char) c));

    scan_unget(in, c);
}

static int scanset_has(const char* set, int c) {
    int invert = 0;

    const char* p = set;

    if (*p == '^') {
        invert = 1;

        p++;
    }

    while (*p != '\0') {
        if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
            if (c >= (unsigned char) p[0] && c <= (unsigned char) p[2])
                return !invert;

            p += 3;
        }
        else {
            if (c == (unsigned char) *p)
                return !invert;

            p++;
        }
    }

    return invert;
}

static const char* parse_scanset(const char* format, char* set, size_t cap) {
    size_t n = 0;

    if (*format == '^' && n + 1 < cap)
        set[n++] = *format++;

    if (*format == ']') { /* first ] is literal in a scanset */
        if (n + 1 < cap)
            set[n++] = *format;

        format++;
    }

    while (*format != '\0' && *format != ']') {
        if (n + 1 < cap)
            set[n++] = *format;

        format++;
    }

    set[n] = '\0';

    return *format == ']' ? format + 1 : format;
}

static int conversion_width(const char** format) {
    int width = 0;

    while (isdigit((unsigned char) **format)) {
        if (width < 100000)
            width = width * 10 + (**format - '0');

        (*format)++;
    }

    return width;
}

static int scan_token(struct scan_input* in, char* token, size_t cap, int width,
                      int (*accept)(int), int skip_space) {
    size_t n = 0;
    int    c;

    if (skip_space)
        skip_input_space(in);

    while (width == 0 || (int) n < width) {
        c = scan_get(in);

        if (c == EOF || !accept(c)) {
            scan_unget(in, c);
            break;
        }

        if (n + 1 < cap)
            token[n++] = (char) c;
    }

    token[n] = '\0';

    return n != 0;
}

static int integer_char(int c) {
    return isalnum((unsigned char) c) || c == '+' || c == '-';
}

static int float_char(int c) {
    return isalnum((unsigned char) c) || c == '+' || c == '-' || c == '.';
}

static int parse_integer(const char* token, char spec, long long* signed_value,
                         unsigned long long* unsigned_value) {
    int base = spec == 'i' ? 0 : spec == 'o' ? 8 : (spec == 'x' || spec == 'X') ? 16 : 10;

    char* end;

    if (spec == 'd' || spec == 'i') {
        long long value = strtoll(token, &end, base);

        if (*end != '\0')
            return 0;

        *signed_value = value;
    }
    else {
        unsigned long long value = strtoull(token, &end, base);

        if (*end != '\0')
            return 0;

        *unsigned_value = value;
    }

    return 1;
}

static int scan_engine(struct scan_input* in, const char* format, va_list ap) {
    int assigned = 0;
    int matched  = 1;

    const char* p = format;

    while (*p != '\0' && matched) {
        if (isspace((unsigned char) *p)) {
            while (isspace((unsigned char) *p)) {
                p++;
            }

            skip_input_space(in);
            continue;
        }

        if (*p != '%') {
            int c = scan_get(in);

            if (c != (unsigned char) *p) {
                scan_unget(in, c);

                matched = 0;
            }

            p++;
            continue;
        }

        p++;

        if (*p == '%') {
            int c = scan_get(in);
            if (c != '%') {
                scan_unget(in, c);

                matched = 0;
            }

            p++;
            continue;
        }

        {
            int  suppress = 0;
            int  width;
            char length = 0;
            char spec;
            char token[256];
            int  ok = 0;

            if (*p == '*') {
                suppress = 1;

                p++;
            }

            width = conversion_width(&p);

            if (*p == 'h' || *p == 'l' || *p == 'L' || *p == 'z') {
                length = *p++;

                if ((length == 'h' && *p == 'h') || (length == 'l' && *p == 'l')) {
                    p++;

                    length = length == 'h' ? 'H' : 'L';
                }
            }

            spec = *p++;

            if (spec == 'd' || spec == 'i' || spec == 'o' || spec == 'u' || spec == 'x' ||
                spec == 'X' || spec == 'p') {
                if (spec == 'p') {
                    char* end;

                    unsigned long long value;

                    ok    = scan_token(in, token, sizeof token, width, integer_char, 1);
                    value = strtoull(token, &end, 16);
                    ok    = ok && *end == '\0';

                    if (ok && !suppress)
                        *va_arg(ap, void**) = (void*) (uintptr_t) value;
                }
                else {
                    long long          signed_value   = 0;
                    unsigned long long unsigned_value = 0;

                    ok = scan_token(in, token, sizeof token, width, integer_char, 1);
                    if (ok)
                        ok = parse_integer(token, spec, &signed_value, &unsigned_value);

                    if (ok && !suppress) {
                        if (spec == 'd' || spec == 'i') {
                            if (length == 'L')
                                *va_arg(ap, long long*) = signed_value;
                            else if (length == 'l')
                                *va_arg(ap, long*) = (long) signed_value;
                            else if (length == 'H')
                                *va_arg(ap, signed char*) = (signed char) signed_value;
                            else if (length == 'h')
                                *va_arg(ap, short*) = (short) signed_value;
                            else if (length == 'z')
                                *va_arg(ap, long*) = (long) signed_value;
                            else
                                *va_arg(ap, int*) = (int) signed_value;
                        }
                        else {
                            if (length == 'L')
                                *va_arg(ap, unsigned long long*) = unsigned_value;
                            else if (length == 'l')
                                *va_arg(ap, unsigned long*) =
                                    (unsigned long) unsigned_value;
                            else if (length == 'H')
                                *va_arg(ap, unsigned char*) =
                                    (unsigned char) unsigned_value;
                            else if (length == 'h')
                                *va_arg(ap, unsigned short*) =
                                    (unsigned short) unsigned_value;
                            else if (length == 'z')
                                *va_arg(ap, size_t*) = (size_t) unsigned_value;
                            else
                                *va_arg(ap, unsigned int*) =
                                    (unsigned int) unsigned_value;
                        }
                    }
                }
            }
            else if (spec == 'f' || spec == 'e' || spec == 'E' || spec == 'g' ||
                     spec == 'G' || spec == 'a' || spec == 'A') {
                char* end;

                int converted;

                ok = scan_token(in, token, sizeof token, width, float_char, 1);

                if (length == 'L') {
                    long double value = strtold(token, &end);

                    converted = ok && *end == '\0';

                    if (converted && !suppress)
                        *va_arg(ap, long double*) = value;
                }
                else if (length == 'l') {
                    double value = strtod(token, &end);

                    converted = ok && *end == '\0';

                    if (converted && !suppress)
                        *va_arg(ap, double*) = value;
                }
                else {
                    float value = strtof(token, &end);

                    converted = ok && *end == '\0';

                    if (converted && !suppress)
                        *va_arg(ap, float*) = value;
                }

                ok = converted;
            }
            else if (spec == 'c') {
                int n = width == 0 ? 1 : width;
                int i;

                char* dest = suppress ? NULL : va_arg(ap, char*);

                ok = 1;

                for (i = 0; i < n; i++) {
                    int c = scan_get(in);
                    if (c == EOF) {
                        ok = 0;

                        break;
                    }

                    if (!suppress)
                        dest[i] = (char) c;
                }
            }
            else if (spec == 's' || spec == '[') {
                char set[128];

                char* dest = suppress ? NULL : va_arg(ap, char*);

                size_t n = 0;
                int    c;

                if (spec == '[') {
                    p = parse_scanset(p, set, sizeof set);
                }
                else {
                    set[0] = '^';
                    set[1] = '\0';
                }

                if (spec == 's')
                    skip_input_space(in);

                while (width == 0 || (int) n < width) {
                    c = scan_get(in);

                    if (c == EOF || (spec == 's' ? isspace((unsigned char) c)
                                                 : !scanset_has(set, c))) {
                        scan_unget(in, c);
                        break;
                    }

                    if (!suppress)
                        dest[n] = (char) c;

                    n++;
                }

                ok = n != 0;

                if (ok && !suppress)
                    dest[n] = '\0';
            }
            else if (spec == 'n') {
                if (!suppress) {
                    if (length == 'l')
                        *va_arg(ap, long*) = (long) in->count;
                    else
                        *va_arg(ap, int*) = (int) in->count;
                }

                ok = 1;
            }
            else {
                matched = 0;

                continue;
            }

            if (!ok)
                matched = 0;
            else if (!suppress && spec != 'n')
                assigned++;
        }
    }

    return assigned == 0 && in->input_eof ? EOF : assigned;
}

int vsscanf(const char* input, const char* format, va_list ap) {
    struct string_input data = {(const unsigned char*) input};
    struct scan_input   in   = {string_get, string_unget, &data, 0, 0};

    return scan_engine(&in, format, ap);
}

int sscanf(const char* input, const char* format, ...) {
    va_list ap;

    int rc;

    va_start(ap, format);

    rc = vsscanf(input, format, ap);

    va_end(ap);

    return rc;
}

int vfscanf(FILE* stream, const char* format, va_list ap) {
    struct scan_input in = {file_get, file_unget, stream, 0, 0};

    if (stream == NULL)
        return EOF;

    return scan_engine(&in, format, ap);
}

int fscanf(FILE* stream, const char* format, ...) {
    va_list ap;

    int rc;

    va_start(ap, format);

    rc = vfscanf(stream, format, ap);

    va_end(ap);

    return rc;
}

int vscanf(const char* format, va_list ap) {
    return vfscanf(stdin, format, ap);
}

int scanf(const char* format, ...) {
    va_list ap;

    int rc;

    va_start(ap, format);

    rc = vscanf(format, ap);

    va_end(ap);

    return rc;
}
