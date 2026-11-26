#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * astia libc — printf-family formatting.
 *
 * One sink does all output: `len` counts the length the call *would*
 * produce (the return value), while bytes land only where they fit, with
 * a terminating NUL whenever size > 0.  size == 0 or a NULL buffer turns
 * the call into a pure length computation — the snprintf contract.
 *
 * Thread-safety: none of this touches global state — sink, cursor, and
 * parsed fields are locals, the varargs cursor belongs to the call.  Any
 * number of threads can format at once into their own buffers; a shared
 * output buffer is the caller's race, as with any write.
 *
 * Floating output is implemented locally for the decimal and hexadecimal
 * forms (%f/%e/%g/%a and their uppercase variants), including finite
 * values, infinities, NaNs, signs, width and precision.
 */

struct sink {
    char*  buf;
    size_t cap;
    size_t len;
};

#define FLOAT_BODY_LIMIT 1000
#define FLOAT_BODY_SIZE 1002
#define FLOAT_EXP_RESERVE 7

static void sink_putc(struct sink* s, char c) {
    if (s->buf != NULL && s->len + 1 < s->cap)
        s->buf[s->len] = c;

    s->len++;
}

static void sink_finish(struct sink* s) {
    if (s->buf != NULL && s->cap > 0) {
        size_t at = (s->len < s->cap - 1) ? s->len : s->cap - 1;

        s->buf[at] = '\0';
    }
}

static void sink_pad(struct sink* s, char c, size_t n) {
    while (n--) {
        sink_putc(s, c);
    }
}

/* Reverse-ordered digits of v; returns how many. */
static size_t uconv(char* tmp, unsigned long long v, unsigned base, int upper) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";

    size_t n = 0;

    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v != 0);

    return n;
}

static void emit_reversed(struct sink* s, const char* tmp, size_t n) {
    while (n--) {
        sink_putc(s, tmp[n]);
    }
}

static int float_negative(double value) {
    uint64_t bits;

    memcpy(&bits, &value, sizeof bits);

    return (bits >> 63) != 0;
}

static int float_special(double value, int* inf, int* nan) {
    uint64_t bits;
    uint64_t exponent;
    uint64_t fraction;

    memcpy(&bits, &value, sizeof bits);

    exponent = (bits >> 52) & UINT64_C(0x7ff);
    fraction = bits & UINT64_C(0xfffffffffffff);
    *inf     = exponent == UINT64_C(0x7ff) && fraction == 0;
    *nan     = exponent == UINT64_C(0x7ff) && fraction != 0;

    return *inf || *nan;
}

static void float_append(char* out, size_t* n, char c) {
    if (*n < FLOAT_BODY_LIMIT)
        out[(*n)++] = c;
}

static void float_round(char* digits, size_t n, int round_up) {
    size_t i = n;

    if (!round_up)
        return;

    while (i != 0) {
        i--;

        if (digits[i] != '9') {
            digits[i]++;

            return;
        }

        digits[i] = '0';
    }
}

static size_t float_exp_body(char* out, double value, size_t precision, int alternate) {
    long double x        = (long double) value;
    int         exponent = 0;
    size_t      n        = 0;
    size_t      i;
    char        digits[FLOAT_BODY_SIZE];
    int         next;

    if (precision > FLOAT_BODY_LIMIT - FLOAT_EXP_RESERVE)
        precision = FLOAT_BODY_LIMIT - FLOAT_EXP_RESERVE;

    if (x == 0.0L) {
        float_append(out, &n, '0');

        if (precision != 0 || alternate)
            float_append(out, &n, '.');

        for (i = 0; i < precision; i++) {
            float_append(out, &n, '0');
        }

        exponent = 0;
    }
    else {
        while (x >= 10.0L && exponent < 400) {
            x /= 10.0L;

            exponent++;
        }

        while (x < 1.0L && exponent > -400) {
            x *= 10.0L;

            exponent--;
        }

        digits[0] = (char) ('0' + (int) x);
        x         = (x - (long double) (digits[0] - '0')) * 10.0L;

        for (i = 1; i <= precision; i++) {
            digits[i] = (char) ('0' + (int) x);
            x         = (x - (long double) (digits[i] - '0')) * 10.0L;
        }

        next = (int) x;

        float_round(digits, precision + 1, next >= 5);

        if (digits[0] == '0') {
            digits[0] = '1';

            exponent++;
        }

        float_append(out, &n, digits[0]);

        if (precision != 0 || alternate)
            float_append(out, &n, '.');

        for (i = 1; i <= precision; i++) {
            float_append(out, &n, digits[i]);
        }
    }

    float_append(out, &n, 'e');
    float_append(out, &n, exponent < 0 ? '-' : '+');

    if (exponent < 0)
        exponent = -exponent;

    if (exponent < 10)
        float_append(out, &n, '0');

    {
        char   rev[8];
        size_t rn = 0;

        do {
            rev[rn++] = (char) ('0' + exponent % 10);
            exponent /= 10;
        } while (exponent != 0);

        while (rn != 0) {
            float_append(out, &n, rev[--rn]);
        }
    }

    out[n] = '\0';

    return n;
}

static size_t float_fixed_body(char* out, double value, size_t precision, int alternate) {
    long double x = (long double) value;
    long double work;
    int         exponent = 0;
    size_t      n        = 0;
    size_t      i;
    size_t      integer_digits;
    char        digits[FLOAT_BODY_SIZE];
    int         next;

    if (x == 0.0L) {
        if (precision > FLOAT_BODY_LIMIT - 2)
            precision = FLOAT_BODY_LIMIT - 2;

        float_append(out, &n, '0');

        if (precision != 0 || alternate)
            float_append(out, &n, '.');

        for (i = 0; i < precision; i++) {
            float_append(out, &n, '0');
        }

        out[n] = '\0';

        return n;
    }

    work = x;

    while (work >= 10.0L && exponent < 400) {
        work /= 10.0L;

        exponent++;
    }

    while (work < 1.0L && exponent > -400) {
        work *= 10.0L;

        exponent--;
    }

    if (exponent >= 0) {
        integer_digits = (size_t) exponent + 1;

        for (i = 0; i < integer_digits; i++) {
            digits[i] = (char) ('0' + (int) work);
            work      = (work - (long double) (digits[i] - '0')) * 10.0L;
        }
    }
    else {
        integer_digits = 1;
        digits[0]      = '0';
    }

    if (exponent < 0) {
        size_t zeros = (size_t) (-exponent - 1);

        for (i = 0; i < zeros; i++) {
            digits[integer_digits + i] = '0';
        }

        integer_digits += zeros;
        work = x;

        while (work < 1.0L) {
            work *= 10.0L;
        }
    }

    if (precision != 0 || alternate)
        digits[integer_digits] = '.';

    {
        size_t at = integer_digits + ((precision != 0 || alternate) ? 1 : 0);

        if (precision > FLOAT_BODY_LIMIT - 1 - at)
            precision = FLOAT_BODY_LIMIT - 1 - at;

        for (i = 0; i < precision; i++) {
            int digit = (int) work;

            digits[at + i] = (char) ('0' + digit);
            work           = (work - (long double) digit) * 10.0L;
        }

        next = (int) work;

        {
            size_t total = at + precision;
            size_t j     = total;

            if (next >= 5) {
                while (j != 0) {
                    j--;

                    if (digits[j] == '.')
                        continue;

                    if (digits[j] != '9') {
                        digits[j]++;
                        break;
                    }

                    digits[j] = '0';
                }

                if (j == 0 && digits[0] == '0') {
                    memmove(digits + 1, digits, total);

                    digits[0] = '1';

                    total++;
                }
            }

            for (i = 0; i < total; i++) {
                float_append(out, &n, digits[i]);
            }
        }
    }

    out[n] = '\0';

    return n;
}

static size_t float_hex_body(char* out, double value, size_t precision,
                             int precision_set) {
    uint64_t bits;
    uint64_t fraction;

    unsigned exponent_bits;
    int      exponent;
    size_t   n = 0;
    size_t   digits;
    size_t   i;

    memcpy(&bits, &value, sizeof bits);

    fraction      = bits & UINT64_C(0xfffffffffffff);
    exponent_bits = (unsigned) ((bits >> 52) & UINT64_C(0x7ff));

    float_append(out, &n, '0');
    float_append(out, &n, 'x');

    if (exponent_bits == 0) {
        exponent = -1022;

        float_append(out, &n, '0');
    }
    else {
        exponent = (int) exponent_bits - 1023;

        float_append(out, &n, '1');
    }

    float_append(out, &n, '.');

    digits = precision_set ? precision : 13;
    if (digits > 1000)
        digits = 1000;

    for (i = 0; i < digits; i++) {
        unsigned shift  = i < 13 ? 52U - (unsigned) (i + 1) * 4U : 0;
        unsigned nibble = i < 13 ? (unsigned) ((fraction >> shift) & 0xf) : 0;

        float_append(out, &n, "0123456789abcdef"[nibble]);
    }

    if (!precision_set) {
        while (n > 0 && out[n - 1] == '0') {
            n--;
        }

        if (n > 0 && out[n - 1] == '.')
            n--;
    }

    float_append(out, &n, 'p');
    float_append(out, &n, exponent < 0 ? '-' : '+');

    if (exponent < 0)
        exponent = -exponent;

    {
        char   rev[8];
        size_t rn = 0;

        do {
            rev[rn++] = (char) ('0' + exponent % 10);
            exponent /= 10;
        } while (exponent != 0);

        while (rn != 0) {
            float_append(out, &n, rev[--rn]);
        }
    }

    out[n] = '\0';

    return n;
}

static size_t trim_general_exponent(char* out, size_t n) {
    size_t exponent_at = n;
    size_t mantissa_end;
    size_t i;

    for (i = 0; i < n; i++) {
        if (out[i] == 'e' || out[i] == 'E') {
            exponent_at = i;

            break;
        }
    }

    if (exponent_at == n) {
        mantissa_end = n;
    }
    else {
        mantissa_end = exponent_at;

        while (mantissa_end > 0 && out[mantissa_end - 1] == '0') {
            mantissa_end--;
        }

        if (mantissa_end > 0 && out[mantissa_end - 1] == '.')
            mantissa_end--;

        memmove(out + mantissa_end, out + exponent_at, n - exponent_at);

        n = mantissa_end + n - exponent_at;
    }

    while (exponent_at == n && n > 0 && out[n - 1] == '0') {
        out[--n] = '\0';
    }

    if (exponent_at == n && n > 0 && out[n - 1] == '.')
        out[--n] = '\0';

    out[n] = '\0';

    return n;
}

static size_t float_body(char* out, double value, char spec, size_t precision,
                         int precision_set, int alternate) {
    int    inf;
    int    nan;
    size_t i;
    size_t n;

    if (float_special(value, &inf, &nan)) {
        const char* text =
            nan ? (spec == 'F' || spec == 'E' || spec == 'G' ? "NAN" : "nan")
                : (spec == 'F' || spec == 'E' || spec == 'G' ? "INF" : "inf");

        memcpy(out, text, 4);

        out[inf ? 3 : 3] = '\0';

        return 3;
    }

    if (!precision_set)
        precision = 6;

    if (spec == 'a' || spec == 'A')
        return float_hex_body(out, value, precision, precision_set);

    if (spec == 'f' || spec == 'F')
        return float_fixed_body(out, value, precision, alternate);

    if (spec == 'e' || spec == 'E') {
        n = float_exp_body(out, value, precision, alternate);

        if (spec == 'E') {
            for (i = 0; i < n; i++) {
                if (out[i] == 'e')
                    out[i] = 'E';
            }
        }

        return n;
    }

    if (precision == 0)
        precision = 1;

    {
        long double x        = (long double) value;
        int         exponent = 0;

        while (x != 0.0L && x >= 10.0L) {
            x /= 10.0L;

            exponent++;
        }

        while (x != 0.0L && x < 1.0L) {
            x *= 10.0L;

            exponent--;
        }

        if (exponent < -4 || exponent >= (int) precision) {
            n = float_exp_body(out, value, precision - 1, alternate);
        }
        else {
            size_t fixed_precision = exponent >= 0 && (size_t) (exponent + 1) < precision
                                         ? precision - (size_t) (exponent + 1)
                                         : 0;

            n = float_fixed_body(out, value, fixed_precision, alternate);
        }

        if (!alternate)
            n = trim_general_exponent(out, n);

        return n;
    }
}

int vsnprintf(char* str, size_t size, const char* fmt, va_list ap) {
    struct sink s;
    const char* p = fmt;

    s.buf = str;
    s.cap = size;
    s.len = 0;

    while (*p != '\0') {
        char   tmp[64];
        size_t ndig = 0;

        const char* prefix = "";

        size_t             plen     = 0;
        size_t             prec     = (size_t) -1;
        int                prec_set = 0;
        int                left     = 0;
        int                zero     = 0;
        int                alt      = 0;
        int                sign     = 0; /* '+', ' ', or none */
        int                width    = 0;
        int                len_mod  = 0; /* 0, h=1, hh=2, l=3, ll=4, z=5 */
        unsigned long long mag      = 0;
        unsigned           base     = 10;
        char               spec;

        if (*p != '%') {
            sink_putc(&s, *p++);
            continue;
        }

        p++;

        if (*p == '%') {
            sink_putc(&s, '%');
            p++;
            continue;
        }

        if (*p == '\0') {
            sink_putc(&s, '%');
            break;
        }

        for (;;) { /* flags */
            if (*p == '-') {
                left = 1;
            }
            else if (*p == '0') {
                zero = 1;
            }
            else if (*p == '+') {
                sign = '+';
            }
            else if (*p == ' ') {
                if (!sign)
                    sign = ' ';
            }
            else if (*p == '#') {
                alt = 1;
            }
            else {
                break;
            }

            p++;
        }

        if (*p == '*') { /* width */
            width = va_arg(ap, int);

            p++;

            if (width < 0) {
                left  = 1;
                width = -width;
            }
        }
        else {
            while (isdigit((unsigned char) *p)) {
                if (width <= 100000)
                    width = width * 10 + (*p - '0');

                p++;
            }
        }

        if (*p == '.') { /* precision */
            p++;

            if (*p == '*') {
                int pv = va_arg(ap, int);

                p++;

                if (pv >= 0) {
                    prec     = (size_t) pv;
                    prec_set = 1;
                }
            }
            else {
                prec     = 0;
                prec_set = 1;

                while (isdigit((unsigned char) *p)) {
                    if (prec <= 100000)
                        prec = prec * 10 + (size_t) (*p - '0');

                    p++;
                }
            }
        }

        if (*p == 'h') { /* length modifiers */
            p++;

            if (*p == 'h') {
                p++;

                len_mod = 2;
            }
            else {
                len_mod = 1;
            }
        }
        else if (*p == 'l') {
            p++;

            if (*p == 'l') {
                p++;

                len_mod = 4;
            }
            else {
                len_mod = 3;
            }
        }
        else if (*p == 'z') {
            p++;

            len_mod = 5;
        }

        spec = *p;
        if (spec == '\0') {
            sink_putc(&s, '%');
            break;
        }

        p++;

        if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'o' || spec == 'x' ||
            spec == 'X' || spec == 'p') {
            if (spec == 'd' || spec == 'i') {
                long long v;

                switch (len_mod) {
                case 1:
                    v = (short) va_arg(ap, int);

                    break;
                case 2:
                    v = (signed char) va_arg(ap, int);

                    break;
                case 3:
                    v = va_arg(ap, long);

                    break;
                case 4:
                    v = va_arg(ap, long long);

                    break;
                case 5:
                    v = va_arg(ap, long);

                    break;
                default:
                    v = va_arg(ap, int);

                    break;
                }

                if (v < 0) {
                    sign = '-';
                    /* -(v+1)+1 avoids the LONG_MIN overflow */
                    mag = (unsigned long long) (-(v + 1)) + 1;
                }
                else {
                    mag = (unsigned long long) v;
                }
            }
            else if (spec == 'p') {
                mag  = (unsigned long long) (uintptr_t) va_arg(ap, void*);
                base = 16;
            }
            else {
                unsigned long long v;

                switch (len_mod) {
                case 1:
                    v = (unsigned short) va_arg(ap, unsigned);

                    break;
                case 2:
                    v = (unsigned char) va_arg(ap, unsigned);

                    break;
                case 3:
                    v = va_arg(ap, unsigned long);

                    break;
                case 4:
                    v = va_arg(ap, unsigned long long);

                    break;
                case 5:
                    v = va_arg(ap, size_t);

                    break;
                default:
                    v = va_arg(ap, unsigned);

                    break;
                }

                sign = 0; /* %+u is a contradiction */
                mag  = v;

                if (spec == 'o')
                    base = 8;
                else if (spec == 'x' || spec == 'X')
                    base = 16;
            }

            ndig = uconv(tmp, mag, base, spec == 'X');

            if (spec == 'p') {
                prefix = "0x";
                plen   = 2;
            }
            else if (alt && spec == 'x' && mag != 0) {
                prefix = "0x";
                plen   = 2;
            }
            else if (alt && spec == 'X' && mag != 0) {
                prefix = "0X";
                plen   = 2;
            }
            else if (alt && spec == 'o' && tmp[ndig - 1] != '0') {
                prefix = "0";
                plen   = 1;
            }

            {
                char   pfx[4];
                size_t pn    = 0;
                size_t zeros = 0;
                size_t body;
                size_t pad;
                int    pad_zero;
                size_t i;

                if (sign)
                    pfx[pn++] = (char) sign;

                memcpy(pfx + pn, prefix, plen);

                pn += plen;

                if (prec_set && prec > ndig)
                    zeros = prec - ndig;

                body     = pn + zeros + ndig;
                pad      = ((size_t) width > body) ? (size_t) width - body : 0;
                pad_zero = zero && !left && !prec_set;

                if (!left && !pad_zero)
                    sink_pad(&s, ' ', pad);

                for (i = 0; i < pn; i++) {
                    sink_putc(&s, pfx[i]);
                }

                if (pad_zero)
                    sink_pad(&s, '0', pad);

                sink_pad(&s, '0', zeros);
                emit_reversed(&s, tmp, ndig);

                if (left)
                    sink_pad(&s, ' ', pad);
            }
        }
        else if (spec == 'n') {
            switch (len_mod) {
            case 1:
                *va_arg(ap, short*) = (short) s.len;

                break;
            case 2:
                *va_arg(ap, signed char*) = (signed char) s.len;

                break;
            case 3:
                *va_arg(ap, long*) = (long) s.len;

                break;
            case 4:
                *va_arg(ap, long long*) = (long long) s.len;

                break;
            case 5:
                *va_arg(ap, ptrdiff_t*) = (ptrdiff_t) s.len;

                break;
            default:
                *va_arg(ap, int*) = (int) s.len;

                break;
            }
        }
        else if (spec == 'c') {
            char   c   = (char) va_arg(ap, int);
            size_t pad = (width > 1) ? (size_t) width - 1 : 0;

            if (!left)
                sink_pad(&s, ' ', pad);

            sink_putc(&s, c);

            if (left)
                sink_pad(&s, ' ', pad);
        }
        else if (spec == 's') {
            const char* strarg = va_arg(ap, const char*);

            size_t slen;
            size_t pad;
            size_t i;

            if (strarg == NULL) {
                strarg = "(null)";
                slen   = 6;
            }
            else {
                slen = strlen(strarg);
            }

            if (prec_set && slen > prec)
                slen = prec;

            pad = ((size_t) width > slen) ? (size_t) width - slen : 0;

            if (!left)
                sink_pad(&s, ' ', pad);

            for (i = 0; i < slen; i++) {
                sink_putc(&s, strarg[i]);
            }

            if (left)
                sink_pad(&s, ' ', pad);
        }
        else if (spec == 'a' || spec == 'A' || spec == 'e' || spec == 'E' ||
                 spec == 'f' || spec == 'F' || spec == 'g' || spec == 'G') {
            char   body_buf[FLOAT_BODY_SIZE];
            double value = va_arg(ap, double);
            size_t body_len;
            size_t prefix_len;
            size_t body_pad;
            size_t i;
            int    negative = float_negative(value);
            int    upper    = spec == 'A' || spec == 'E' || spec == 'F' || spec == 'G';

            if (negative)
                value = -value;

            body_len   = float_body(body_buf, value, spec, prec, prec_set, alt);
            prefix_len = (negative || sign != 0) ? 1 : 0;
            body_pad   = ((size_t) width > prefix_len + body_len)
                             ? (size_t) width - prefix_len - body_len
                             : 0;

            if (!left && !zero)
                sink_pad(&s, ' ', body_pad);

            if (negative)
                sink_putc(&s, '-');
            else if (sign != 0)
                sink_putc(&s, (char) sign);

            if (!left && zero)
                sink_pad(&s, '0', body_pad);

            for (i = 0; i < body_len; i++) {
                char c = body_buf[i];

                if (upper && c >= 'a' && c <= 'z')
                    c = (char) (c - 'a' + 'A');

                sink_putc(&s, c);
            }

            if (left)
                sink_pad(&s, ' ', body_pad);
        }
        else {
            sink_putc(&s, '%');
            sink_putc(&s, spec);
        }
    }

    sink_finish(&s);

    return (int) s.len;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;

    int r;

    va_start(ap, format);

    r = vsnprintf(str, size, format, ap);

    va_end(ap);

    return r;
}

int sprintf(char* str, const char* format, ...) {
    va_list ap;

    int r;

    va_start(ap, format);

    /* "unbounded": INT_MAX is what the int return value can report, so
       the bound stays honest to the contract and analyzable */
    r = vsnprintf(str, (size_t) INT_MAX, format, ap);

    va_end(ap);

    return r;
}

int vsprintf(char* str, const char* format, va_list ap) {
    return vsnprintf(str, (size_t) INT_MAX, format, ap);
}
