#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

static int fails;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            fails++;                                                    \
        }                                                               \
    } while (0)

/* ---------------------------------------------------------------- parsing */

static void test_strtol(void)
{
    char* end;
    const char* empty = "";
    const char* nodigit = "xyz";
    const char* no8 = "888";
    char minbuf[32];

    CHECK(strtol("42", &end, 10) == 42 && *end == '\0');
    CHECK(strtol("  -17x", &end, 10) == -17 && strcmp(end, "x") == 0);
    CHECK(strtol("+8", NULL, 10) == 8);

    /* auto base: 0x/0 prefixes */
    CHECK(strtol("0x1f", &end, 0) == 31 && *end == '\0');
    CHECK(strtol("0x1f", NULL, 16) == 31);   /* prefix allowed in base 16 */
    CHECK(strtol("0777", &end, 0) == 511 && *end == '\0');
    CHECK(strtol("0777", NULL, 10) == 777);  /* ... but not in base 10 */
    CHECK(strtol("10", NULL, 2) == 2);

    /* base 36, either case */
    CHECK(strtol("z", NULL, 36) == 35);
    CHECK(strtol("ZZ", NULL, 36) == 1295);   /* 35*36 + 35 */

    /* no conversion: value 0, endptr stays at nptr */
    CHECK(strtol(empty, &end, 10) == 0 && end == empty);
    CHECK(strtol(nodigit, &end, 10) == 0 && end == nodigit);
    /* '8' is no digit in base 8: still no conversion */
    CHECK(strtol(no8, &end, 8) == 0 && end == no8);

    /* saturation */
    errno = 0;
    CHECK(strtol("99999999999999999999999999", NULL, 10) == LONG_MAX);
    CHECK(errno == ERANGE);
    errno = 0;
    CHECK(strtol("-99999999999999999999999999", NULL, 10) == LONG_MIN);
    CHECK(errno == ERANGE);

    /* the exact boundary round-trips, both ends */
    CHECK(snprintf(minbuf, sizeof minbuf, "%lld", (long long) LONG_MIN) > 0);
    CHECK(strtol(minbuf, NULL, 10) == LONG_MIN);
    CHECK(strtol("9223372036854775807", NULL, 10) == LONG_MAX);

    /* bad base: predictable zero, endptr untouched */
    CHECK(strtol(nodigit, &end, 1) == 0 && end == nodigit);
}

static void test_strtoul(void)
{
    char* end;

    CHECK(strtoul("abc", &end, 16) == 0xabcUL && *end == '\0');
    CHECK(strtoul("42", NULL, 10) == 42);
    CHECK(strtoul("-1", NULL, 10) == ULONG_MAX);  /* wraps, per the rules */
    errno = 0;
    CHECK(strtoul("99999999999999999999999999", NULL, 10) == ULONG_MAX);
    CHECK(errno == ERANGE);
}

static void test_atoi(void)
{
    CHECK(atoi("123") == 123);
    CHECK(atoi("-456") == -456);
    CHECK(atoi("  78trailing") == 78);
    CHECK(atoi("abc") == 0);
    CHECK(atol("-7") == -7);
    CHECK(abs(-5) == 5 && abs(5) == 5 && abs(0) == 0);
    CHECK(labs(-1234567890123L) == 1234567890123L);
}

/* ----------------------------------------------------------------- sorting */

static int cmp_int(const void* a, const void* b)
{
    int x = *(const int*) a;
    int y = *(const int*) b;

    return (x > y) - (x < y);
}

struct item {
    int key;
    int tag;
};

static int cmp_item(const void* a, const void* b)
{
    return cmp_int(&((const struct item*) a)->key,
                   &((const struct item*) b)->key);
}

static void check_sorted(const int* a, size_t n, int line)
{
    for (size_t i = 0; i + 1 < n; i++)
        if (a[i] > a[i + 1]) {
            printf("FAIL stdlib_test:%d: unsorted at %zu (%d > %d)\n",
                   line, i, a[i], a[i + 1]);
            fails++;
            return;
        }
}

/* same elements in, same elements out — not just "looks sorted" */
static void check_same_multiset(const int* x, size_t nx,
                                const int* y, size_t ny, int line)
{
    if (nx != ny) {
        printf("FAIL stdlib_test:%d: length %zu vs %zu\n", line, nx, ny);
        fails++;
        return;
    }
    for (size_t i = 0; i < nx; i++) {
        size_t cx = 0;
        size_t cy = 0;
        for (size_t j = 0; j < nx; j++) {
            if (x[j] == x[i])
                cx++;
            if (y[j] == x[i])
                cy++;
        }
        if (cx != cy) {
            printf("FAIL stdlib_test:%d: value %d count %zu vs %zu\n",
                   line, x[i], cx, cy);
            fails++;
            return;
        }
    }
}

static void test_qsort_bsearch(void)
{
    static const int orig[] = {5, 3, 9, 1, 5, 8, 0, -2, 7, 5, -100, 42};
    static const int sorted_in[] = {1, 2, 3, 4, 5, 6, 7, 8};
    static const int reversed[] = {9, 7, 5, 3, 1, -1, -3, -5, -7, -9, 0};
    static const int equal[] = {7, 7, 7, 7, 7};
    int work[sizeof orig / sizeof orig[0]];
    size_t n = sizeof orig / sizeof orig[0];

    memcpy(work, orig, sizeof orig);
    qsort(work, n, sizeof work[0], cmp_int);
    check_sorted(work, n, __LINE__);
    check_same_multiset(orig, n, work, n, __LINE__);

    memcpy(work, sorted_in, sizeof sorted_in);
    qsort(work, sizeof sorted_in / sizeof sorted_in[0], sizeof work[0],
          cmp_int);
    check_sorted(work, sizeof sorted_in / sizeof sorted_in[0], __LINE__);

    memcpy(work, reversed, sizeof reversed);
    qsort(work, sizeof reversed / sizeof reversed[0], sizeof work[0],
          cmp_int);
    check_sorted(work, sizeof reversed / sizeof reversed[0], __LINE__);

    memcpy(work, equal, sizeof equal);
    qsort(work, sizeof equal / sizeof equal[0], sizeof work[0], cmp_int);
    check_same_multiset(equal, sizeof equal / sizeof equal[0],
                        work, sizeof equal / sizeof equal[0], __LINE__);

    work[0] = 42;
    qsort(work, 1, sizeof work[0], cmp_int);
    CHECK(work[0] == 42);

    /* element size beyond an int: tags must travel with their keys */
    {
        struct item it[] = {{3, 30}, {1, 10}, {2, 20}, {-5, -50}};
        size_t m = sizeof it / sizeof it[0];

        qsort(it, m, sizeof it[0], cmp_item);
        CHECK(it[0].key == -5 && it[0].tag == -50);
        CHECK(it[1].key == 1 && it[1].tag == 10);
        CHECK(it[2].key == 2 && it[2].tag == 20);
        CHECK(it[3].key == 3 && it[3].tag == 30);
    }

    /* bsearch over a sorted array */
    {
        int s[] = {-9, -3, 0, 1, 4, 7, 12, 55};
        size_t m = sizeof s / sizeof s[0];
        int probe = 7;
        int missing = 8;
        int first = -9;
        int last = 55;

        CHECK(bsearch(&probe, s, m, sizeof s[0], cmp_int) == &s[5]);
        CHECK(bsearch(&missing, s, m, sizeof s[0], cmp_int) == NULL);
        CHECK(bsearch(&first, s, m, sizeof s[0], cmp_int) == &s[0]);
        CHECK(bsearch(&last, s, m, sizeof s[0], cmp_int) == &s[m - 1]);
        CHECK(bsearch(&probe, s, 0, sizeof s[0], cmp_int) == NULL);
        CHECK(bsearch(&probe, s, m, 0, cmp_int) == NULL);
    }
}

static void test_rand(void)
{
    int seq[32];
    int i;

    srand(42);
    for (i = 0; i < 32; i++) {
        seq[i] = rand();
        CHECK(seq[i] >= 0 && seq[i] <= RAND_MAX);
    }

    /* same seed, same sequence */
    srand(42);
    for (i = 0; i < 32; i++)
        CHECK(rand() == seq[i]);

    /* different seed, different sequence */
    srand(43);
    CHECK(rand() != seq[0]);
}

int main(void)
{
    test_strtol();
    test_strtoul();
    test_atoi();
    test_qsort_bsearch();
    test_rand();

    if (fails)
        printf("stdlib_test: %d failure(s)\n", fails);
    return fails != 0;
}
