#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern char** environ;

static int environment_name_matches(const char* entry, const char* name, size_t length) {
    return strncmp(entry, name, length) == 0 && entry[length] == '=';
}

char* getenv(const char* name) {
    size_t length;

    char** item;

    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL)
        return NULL;

    length = strlen(name);

    for (item = environ; item != NULL && *item != NULL; ++item) {
        if (environment_name_matches(*item, name, length))
            return *item + length + 1;
    }

    return NULL;
}

static int environment_update(const char* name, const char* value, int remove,
                              int overwrite, char* replacement) {
    size_t length = strlen(name);
    size_t count  = 0;
    size_t match  = (size_t) -1;

    char** item;
    char** next;

    size_t i;
    size_t matches               = 0;
    int    allocated_replacement = 0;

    for (item = environ; item != NULL && *item != NULL; ++item, ++count) {
        if (environment_name_matches(*item, name, length)) {
            if (match == (size_t) -1)
                match = count;

            ++matches;
        }
    }

    if (match != (size_t) -1 && !remove && !overwrite)
        return 0;

    if (!remove && (replacement == NULL)) {
        size_t value_length = strlen(value);

        replacement = (char*) malloc(length + value_length + 2);
        if (replacement == NULL) {
            errno = ENOMEM;

            return -1;
        }

        memcpy(replacement, name, length);

        replacement[length] = '=';

        memcpy(replacement + length + 1, value, value_length + 1);

        allocated_replacement = 1;
    }

    next = (char**) malloc((count - matches + (remove ? 0 : 1) + 1) * sizeof(*next));
    if (next == NULL) {
        if (allocated_replacement)
            free(replacement);

        errno = ENOMEM;

        return -1;
    }

    size_t out_count = 0;

    for (i = 0; i < count; ++i) {
        if (environment_name_matches(environ[i], name, length))
            continue;

        next[out_count++] = environ[i];
    }

    if (!remove)
        next[out_count++] = replacement;

    next[out_count] = NULL;
    environ         = next;

    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL || value == NULL) {
        errno = EINVAL;

        return -1;
    }

    return environment_update(name, value, 0, overwrite != 0, NULL);
}

int unsetenv(const char* name) {
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL) {
        errno = EINVAL;

        return -1;
    }

    return environment_update(name, "", 1, 1, NULL);
}

int putenv(char* string) {
    char* equals;
    char* name;

    int result;

    if (string == NULL || (equals = strchr(string, '=')) == NULL || equals == string) {
        errno = EINVAL;

        return -1;
    }

    name = (char*) malloc((size_t) (equals - string) + 1);
    if (name == NULL) {
        errno = ENOMEM;

        return -1;
    }

    memcpy(name, string, (size_t) (equals - string));

    name[equals - string] = '\0';
    result                = environment_update(name, equals + 1, 0, 1, string);

    free(name);

    return result;
}

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
    const char* s = nptr;

    unsigned long acc = 0;
    int           any = 0;

    *negative = 0;
    *overflow = 0;

    while (isspace((unsigned char) *s)) {
        s++;
    }

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
    const char* s = nptr;

    unsigned long long acc = 0;
    int                any = 0;

    *negative = 0;
    *overflow = 0;

    while (isspace((unsigned char) *s)) {
        s++;
    }

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

intmax_t imaxabs(intmax_t value) {
    return value < 0 ? -value : value;
}

imaxdiv_t imaxdiv(intmax_t numerator, intmax_t denominator) {
    imaxdiv_t result = {numerator / denominator, numerator % denominator};

    return result;
}

intmax_t strtoimax(const char* string, char** end, int base) {
    return (intmax_t) strtoll(string, end, base);
}

uintmax_t strtoumax(const char* string, char** end, int base) {
    return (uintmax_t) strtoull(string, end, base);
}

static int ascii_equal_ci(const char* a, const char* b, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i]))
            return 0;
    }

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

static long double parse_floating(const char* nptr, char** endptr, int* negative_out,
                                  int* nonzero_out) {
    const char* s = nptr;

    long double value             = 0.0L;
    int         negative          = 0;
    int         exponent          = 0;
    int         exponent_negative = 0;
    int         any               = 0;
    int         nonzero           = 0;
    int         frac_digits       = 0;

    while (isspace((unsigned char) *s)) {
        s++;
    }

    if (*s == '+' || *s == '-') {
        negative = (*s == '-');

        s++;
    }

    if (ascii_equal_ci(s, "infinity", 8)) {
        uint64_t bits = UINT64_C(0x7ff0000000000000);

        double result;

        memcpy(&result, &bits, sizeof result);

        if (endptr)
            *endptr = (char*) (s + 8);

        *negative_out = negative;
        *nonzero_out  = 0;

        return (long double) result;
    }

    if (ascii_equal_ci(s, "inf", 3)) {
        uint64_t bits = UINT64_C(0x7ff0000000000000);

        double result;

        memcpy(&result, &bits, sizeof result);

        if (endptr)
            *endptr = (char*) (s + 3);

        *negative_out = negative;
        *nonzero_out  = 0;

        return (long double) result;
    }

    if (ascii_equal_ci(s, "nan", 3)) {
        uint64_t bits = UINT64_C(0x7ff8000000000000);

        double result;

        memcpy(&result, &bits, sizeof result);

        if (endptr)
            *endptr = (char*) (s + 3);

        *negative_out = 0;
        *nonzero_out  = 0;

        return (long double) result;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        const char* hex_start = s;

        int hex_fraction    = 0;
        int binary_exponent = 0;
        int binary_negative = 0;
        int digit;

        s += 2;

        while ((digit = hex_digit_value((unsigned char) *s)) >= 0) {
            value = value * 16.0L + (long double) digit;

            if (digit != 0)
                nonzero = 1;

            s++;

            any = 1;
        }

        if (*s == '.') {
            s++;

            while ((digit = hex_digit_value((unsigned char) *s)) >= 0) {
                value = value * 16.0L + (long double) digit;

                if (digit != 0)
                    nonzero = 1;

                hex_fraction++;
                s++;

                any = 1;
            }
        }

        if (any) {
            if (*s == 'p' || *s == 'P') {
                const char* exponent_start = s;

                int have_exponent = 0;

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
                    s               = exponent_start;
                    binary_negative = 0;
                }
            }

            binary_exponent =
                (binary_negative ? -binary_exponent : binary_exponent) - 4 * hex_fraction;
            value = scale_binary(value, binary_exponent);

            if (endptr)
                *endptr = (char*) s;

            *negative_out = negative;
            *nonzero_out  = nonzero;

            return value;
        }

        s = hex_start;
    }

    while (isdigit((unsigned char) *s)) {
        value = value * 10.0L + (long double) (*s - '0');

        if (*s != '0')
            nonzero = 1;

        s++;

        any = 1;
    }

    if (*s == '.') {
        s++;

        while (isdigit((unsigned char) *s)) {
            value = value * 10.0L + (long double) (*s - '0');

            if (*s != '0')
                nonzero = 1;

            frac_digits++;
            s++;

            any = 1;
        }
    }

    if (!any) {
        if (endptr)
            *endptr = (char*) nptr;

        *negative_out = 0;
        *nonzero_out  = 0;

        return 0.0L;
    }

    exponent -= frac_digits;

    if (*s == 'e' || *s == 'E') {
        const char* ep = s++;

        int e        = 0;
        int have_exp = 0;

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

    if (endptr)
        *endptr = (char*) s;

    *negative_out = negative;
    *nonzero_out  = nonzero;

    return value;
}

static int floating_underflow(long double value, int nonzero, long double minimum) {
    return (value != 0.0L && value < minimum) || (value == 0.0L && nonzero);
}

double strtod(const char* nptr, char** endptr) {
    int negative;
    int nonzero;

    long double value = parse_floating(nptr, endptr, &negative, &nonzero);
    double      result;

    if (value > (long double) DBL_MAX) {
        errno = ERANGE;
        value = (long double) DBL_MAX;
    }
    else if (floating_underflow(value, nonzero, (long double) DBL_MIN)) {
        errno = ERANGE;
        value = 0.0L;
    }

    result = (double) value;

    return negative ? -result : result;
}

float strtof(const char* nptr, char** endptr) {
    int negative;
    int nonzero;

    long double value  = parse_floating(nptr, endptr, &negative, &nonzero);
    float       result = (float) value;

    if (value > (long double) FLT_MAX) {
        if (result > FLT_MAX)
            errno = ERANGE;
    }
    else if (floating_underflow(value, nonzero, (long double) FLT_MIN)) {
        errno = ERANGE;
    }

    return negative ? -result : result;
}

long double strtold(const char* nptr, char** endptr) {
    int negative;
    int nonzero;

    long double value = parse_floating(nptr, endptr, &negative, &nonzero);

    if (value > (long double) LDBL_MAX)
        errno = ERANGE;
    else if (floating_underflow(value, nonzero, (long double) LDBL_MIN))
        errno = ERANGE;

    return negative ? -value : value;
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

        *a++ = *b;
        *b++ = t;
    }
}

static void insertion_sort(char* base, size_t n, size_t w,
                           int (*cmp)(const void*, const void*)) {
    size_t i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0 && cmp(base + j * w, base + (j - 1) * w) < 0; j--) {
            swap_bytes(base + j * w, base + (j - 1) * w, w);
        }
    }
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
        char* lo  = base;
        char* hi  = base + (n - 1) * w;
        char* mid = base + (n / 2) * w;

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
        for (j = 0; j < n - 1; j++) {
            if (cmp(base + j * w, hi) < 0) {
                swap_bytes(base + store * w, base + j * w, w);
                store++;
            }
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
        size_t mid = lo + (hi - lo) / 2;

        const char* elem = (const char*) base + mid * size;

        int c = compar(key, elem);
        if (c > 0)
            lo = mid + 1;
        else if (c < 0)
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
    sigset_t         unblock;
    struct sigaction action = {0};

    if (sigemptyset(&unblock) == 0 && sigaddset(&unblock, SIGABRT) == 0)
        (void) sigprocmask(SIG_UNBLOCK, &unblock, NULL);

    (void) raise(SIGABRT);

    action.sa_handler = SIG_DFL;

    (void) sigemptyset(&action.sa_mask);
    (void) sigaction(SIGABRT, &action, NULL);

    (void) raise(SIGABRT);

    _Exit(127);
}
