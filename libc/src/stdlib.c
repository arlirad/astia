#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Everything here is either pure (conversions, abs, bsearch) or has
 * state that is *defined* to be shared: see the rand/strtol notes below.
 * errno usage goes through the thread-local macro in <errno.h>.
 */

/*
 * Shared base-2..36 parser: whitespace, sign, prefix, digits, and a
 * saturating accumulator.  Once out of range it keeps *consuming* digits
 * (so endptr lands past the number) but stops accumulating.  No static
 * state — the whole result goes back through out-parameters, so two
 * threads can parse at once without knowing about each other.
 */
static void strto_impl(const char* nptr, char** endptr, int base, unsigned long* out,
                       int* negative, int* overflow) {
    const char*   s   = nptr;
    unsigned long acc = 0;
    int           any = 0;

    *negative = 0;
    *overflow = 0;

    while (isspace((unsigned char) *s))
        s++;
    if (*s == '+' || *s == '-') {
        *negative = (*s == '-');
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        isxdigit((unsigned char) s[2])) {
        s += 2;
        base = 16;
    }
    else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    for (;; s++) {
        unsigned char c = (unsigned char) *s;
        int           digit;

        if (isdigit(c))
            digit = c - '0';
        else if (isalpha(c))
            digit = tolower(c) - 'a' + 10;
        else
            break;
        if (digit >= base)
            break;
        any = 1;
        if (!*overflow) {
            if (acc > (ULONG_MAX - (unsigned long) digit) / (unsigned long) base)
                *overflow = 1;
            else
                acc = acc * (unsigned long) base + (unsigned long) digit;
        }
    }

    *out = acc;
    if (endptr)
        *endptr = (char*) (any ? s : nptr);
}

long strtol(const char* nptr, char** endptr, int base) {
    unsigned long acc;
    int           negative, overflow;

    if (base != 0 && (base < 2 || base > 36)) { /* undefined; be predictable */
        if (endptr)
            *endptr = (char*) nptr;
        return 0;
    }

    strto_impl(nptr, endptr, base, &acc, &negative, &overflow);

    if (!overflow) {
        unsigned long limit = (unsigned long) LONG_MAX + (negative ? 1UL : 0UL);
        if (acc > limit) {
            overflow = 1;
        }
        else if (negative) {
            if (acc == (unsigned long) LONG_MAX + 1UL)
                return LONG_MIN; /* magnitude of LONG_MIN */
            return -(long) acc;
        }
        else {
            return (long) acc;
        }
    }

    errno = ERANGE; /* thread-local slot */
    return negative ? LONG_MIN : LONG_MAX;
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    unsigned long acc;
    int           negative, overflow;

    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr)
            *endptr = (char*) nptr;
        return 0;
    }

    strto_impl(nptr, endptr, base, &acc, &negative, &overflow);

    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return negative ? 0UL - acc : acc;
}

static void strtoull_impl(const char* nptr, char** endptr, int base,
                          unsigned long long* out, int* negative, int* overflow) {
    const char*        s   = nptr;
    unsigned long long acc = 0;
    int                any = 0;

    *negative = 0;
    *overflow = 0;
    while (isspace((unsigned char) *s))
        s++;
    if (*s == '+' || *s == '-') {
        *negative = (*s == '-');
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        isxdigit((unsigned char) s[2])) {
        s += 2;
        base = 16;
    }
    else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }
    for (;; s++) {
        unsigned char c = (unsigned char) *s;
        int           digit;

        if (isdigit(c))
            digit = c - '0';
        else if (isalpha(c))
            digit = tolower(c) - 'a' + 10;
        else
            break;
        if (digit >= base)
            break;
        any = 1;
        if (!*overflow) {
            if (acc >
                (ULLONG_MAX - (unsigned long long) digit) / (unsigned long long) base)
                *overflow = 1;
            else
                acc = acc * (unsigned long long) base + (unsigned long long) digit;
        }
    }
    *out = acc;
    if (endptr)
        *endptr = (char*) (any ? s : nptr);
}

long long strtoll(const char* nptr, char** endptr, int base) {
    unsigned long long acc;
    int                negative, overflow;

    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr)
            *endptr = (char*) nptr;
        return 0;
    }
    strtoull_impl(nptr, endptr, base, &acc, &negative, &overflow);
    if (!overflow) {
        unsigned long long limit =
            (unsigned long long) LLONG_MAX + (negative ? 1ULL : 0ULL);
        if (acc <= limit) {
            if (negative) {
                if (acc == (unsigned long long) LLONG_MAX + 1ULL)
                    return LLONG_MIN;
                return -(long long) acc;
            }
            return (long long) acc;
        }
    }
    errno = ERANGE;
    return negative ? LLONG_MIN : LLONG_MAX;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    unsigned long long acc;
    int                negative, overflow;

    if (base != 0 && (base < 2 || base > 36)) {
        if (endptr)
            *endptr = (char*) nptr;
        return 0;
    }
    strtoull_impl(nptr, endptr, base, &acc, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return ULLONG_MAX;
    }
    return negative ? 0ULL - acc : acc;
}

static int ascii_equal_ci(const char* a, const char* b, size_t n) {
    size_t i;

    for (i = 0; i < n; i++)
        if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]))
            return 0;
    return 1;
}

static long double scale_decimal(long double value, int exponent) {
    long double  factor   = 10.0L;
    int          negative = exponent < 0;
    unsigned int n        = (unsigned int) (negative ? -exponent : exponent);

    while (n != 0) {
        if (n & 1U)
            value = negative ? value / factor : value * factor;
        factor *= factor;
        n >>= 1;
    }
    return value;
}

static int hex_digit_value(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static long double scale_binary(long double value, int exponent) {
    long double  factor   = 2.0L;
    int          negative = exponent < 0;
    unsigned int n        = (unsigned int) (negative ? -exponent : exponent);

    while (n != 0) {
        if (n & 1U)
            value = negative ? value / factor : value * factor;
        factor *= factor;
        n >>= 1;
    }
    return value;
}

double strtod(const char* nptr, char** endptr) {
    const char* s                 = nptr;
    long double value             = 0.0L;
    int         negative          = 0;
    int         exponent          = 0;
    int         exponent_negative = 0;
    int         any               = 0;
    int         frac_digits       = 0;

    while (isspace((unsigned char) *s))
        s++;
    if (*s == '+' || *s == '-') {
        negative = (*s == '-');
        s++;
    }
    if (ascii_equal_ci(s, "infinity", 8)) {
        uint64_t bits = UINT64_C(0x7ff0000000000000);
        double   result;
        memcpy(&result, &bits, sizeof result);
        if (negative)
            result = -result;
        if (endptr)
            *endptr = (char*) (s + 8);
        return result;
    }
    if (ascii_equal_ci(s, "inf", 3)) {
        uint64_t bits = UINT64_C(0x7ff0000000000000);
        double   result;
        memcpy(&result, &bits, sizeof result);
        if (negative)
            result = -result;
        if (endptr)
            *endptr = (char*) (s + 3);
        return result;
    }
    if (ascii_equal_ci(s, "nan", 3)) {
        uint64_t bits = UINT64_C(0x7ff8000000000000);
        double   result;
        memcpy(&result, &bits, sizeof result);
        if (endptr)
            *endptr = (char*) (s + 3);
        return result;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        const char* hex_start       = s;
        int         hex_fraction    = 0;
        int         binary_exponent = 0;
        int         binary_negative = 0;
        int         have_exponent   = 0;
        int         digit;

        s += 2;
        while ((digit = hex_digit_value((unsigned char) *s)) >= 0) {
            value = value * 16.0L + (long double) digit;
            s++;
            any = 1;
        }
        if (*s == '.') {
            s++;
            while ((digit = hex_digit_value((unsigned char) *s)) >= 0) {
                value = value * 16.0L + (long double) digit;
                hex_fraction++;
                s++;
                any = 1;
            }
        }
        if (!any || (*s != 'p' && *s != 'P')) {
            if (endptr)
                *endptr = (char*) nptr;
            return 0.0;
        }
        s++;
        if (*s == '+' || *s == '-') {
            binary_negative = (*s == '-');
            s++;
        }
        while (isdigit((unsigned char) *s)) {
            if (binary_exponent < 10000)
                binary_exponent = binary_exponent * 10 + (*s - '0');
            s++;
            have_exponent = 1;
        }
        if (!have_exponent) {
            if (endptr)
                *endptr = (char*) hex_start;
            return 0.0;
        }
        binary_exponent =
            (binary_negative ? -binary_exponent : binary_exponent) - 4 * hex_fraction;
        value = scale_binary(value, binary_exponent);
        if (value > (long double) DBL_MAX) {
            errno = ERANGE;
            value = (long double) DBL_MAX;
        }
        if (endptr)
            *endptr = (char*) s;
        return negative ? -(double) value : (double) value;
    }
    while (isdigit((unsigned char) *s)) {
        value = value * 10.0L + (long double) (*s - '0');
        s++;
        any = 1;
    }
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char) *s)) {
            value = value * 10.0L + (long double) (*s - '0');
            frac_digits++;
            s++;
            any = 1;
        }
    }
    if (!any) {
        if (endptr)
            *endptr = (char*) nptr;
        return 0.0;
    }
    exponent -= frac_digits;
    if (*s == 'e' || *s == 'E') {
        const char* ep       = s++;
        int         e        = 0;
        int         have_exp = 0;
        if (*s == '+' || *s == '-') {
            exponent_negative = (*s == '-');
            s++;
        }
        while (isdigit((unsigned char) *s)) {
            if (e < 10000)
                e = e * 10 + (*s - '0');
            s++;
            have_exp = 1;
        }
        if (!have_exp) {
            s                 = ep;
            exponent_negative = 0;
        }
        else {
            exponent += exponent_negative ? -e : e;
        }
    }
    value = scale_decimal(value, exponent);
    if (value > (long double) DBL_MAX) {
        errno = ERANGE;
        value = (long double) DBL_MAX;
    }
    else if (value != 0.0L && value < (long double) DBL_MIN) {
        errno = ERANGE;
        value = 0.0L;
    }
    if (endptr)
        *endptr = (char*) s;
    return negative ? -(double) value : (double) value;
}

float strtof(const char* nptr, char** endptr) {
    return (float) strtod(nptr, endptr);
}

long double strtold(const char* nptr, char** endptr) {
    return (long double) strtod(nptr, endptr);
}

double atof(const char* nptr) {
    return strtod(nptr, NULL);
}

int atoi(const char* nptr) {
    return (int) strtol(nptr, NULL, 10);
}

long atol(const char* nptr) {
    return strtol(nptr, NULL, 10);
}

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

/* ------------------------------------------------------------------ sorting */

static void swap_bytes(char* a, char* b, size_t w) {
    while (w--) {
        char t = *a;
        *a++   = *b;
        *b++   = t;
    }
}

static void insertion_sort(char* base, size_t n, size_t w,
                           int (*cmp)(const void*, const void*)) {
    size_t i, j;

    for (i = 1; i < n; i++)
        for (j = i; j > 0 && cmp(base + j * w, base + (j - 1) * w) < 0; j--)
            swap_bytes(base + j * w, base + (j - 1) * w, w);
}

/*
 * Quicksort with median-of-three pivot and an insertion-sort cutoff.
 * Recurse into the smaller partition, loop on the larger one, so stack
 * depth stays logarithmic even on adversarial input.  Stateless apart
 * from its own stack: concurrent qsorts on *disjoint* arrays are safe;
 * one array shared across threads is the caller's problem (as everywhere).
 */
static void quicksort(char* base, size_t n, size_t w,
                      int (*cmp)(const void*, const void*)) {
    while (n > 8) {
        char*  lo    = base;
        char*  hi    = base + (n - 1) * w;
        char*  mid   = base + (n / 2) * w;
        size_t store = 0;
        size_t j;

        /* order lo <= mid <= hi, then park the median (mid) at hi */
        if (cmp(lo, mid) > 0)
            swap_bytes(lo, mid, w);
        if (cmp(lo, hi) > 0)
            swap_bytes(lo, hi, w);
        if (cmp(mid, hi) > 0)
            swap_bytes(mid, hi, w);
        swap_bytes(mid, hi, w);

        /* Lomuto partition around the pivot at hi */
        for (j = 0; j < n - 1; j++)
            if (cmp(base + j * w, hi) < 0) {
                swap_bytes(base + store * w, base + j * w, w);
                store++;
            }
        swap_bytes(base + store * w, hi, w);

        if (store < n - 1 - store) {
            quicksort(base, store, w, cmp);
            base += (store + 1) * w;
            n -= store + 1;
        }
        else {
            quicksort(base + (store + 1) * w, n - store - 1, w, cmp);
            n = store;
        }
    }
    insertion_sort(base, n, w, cmp);
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*)) {
    if (nmemb > 1 && size > 0)
        quicksort((char*) base, nmemb, size, compar);
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    size_t lo = 0;
    size_t hi = nmemb;

    if (size == 0)
        return NULL;
    while (lo < hi) {
        size_t      mid  = lo + (hi - lo) / 2;
        const char* elem = (const char*) base + mid * size;
        int         c    = compar(elem, key);
        if (c < 0)
            lo = mid + 1;
        else if (c > 0)
            hi = mid;
        else
            return (void*) elem; /* explicit cast from const char* */
    }
    return NULL;
}

/* -------------------------------------------------------------------- random */

/*
 * The sequence is *specified* to be shared state (one seed, one stream),
 * so this stays a plain global — making it per-thread would silently
 * change "srand once, rand everywhere" semantics.  Concurrent calls race;
 * a locked rand() or a rand_r-style reentrant entry point comes with the
 * thread runtime, same as everyone else's answer to this.
 */
static unsigned long rand_state = 1;

int rand(void) {
    rand_state = rand_state * 1103515245UL + 12345UL;
    return (int) ((rand_state / 65536UL) % (RAND_MAX + 1UL));
}

void srand(unsigned seed) {
    rand_state = seed;
}

/* ------------------------------------------------------------------- program */

void abort(void) {
    /* No signal machinery and no process runtime yet: a trap is the one
       abnormal-stop that means the same thing everywhere. */
    __builtin_trap();
}
