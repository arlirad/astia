#include <stdint.h>
#include <stdio.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static void test_integer_prefixes(void) {
    int      n;
    int      pos;
    unsigned u;

    n   = 0;
    pos = -1;

    CHECK(sscanf("12abc", "%d%n", &n, &pos) == 1);
    CHECK(n == 12 && pos == 2);

    CHECK(sscanf("-12abc", "%d%n", &n, &pos) == 1);
    CHECK(n == -12 && pos == 3);

    CHECK(sscanf("+7x", "%d%n", &n, &pos) == 1);
    CHECK(n == 7 && pos == 2);

    CHECK(sscanf("12.5", "%d%n", &n, &pos) == 1);
    CHECK(n == 12 && pos == 2);

    CHECK(sscanf("0x10zz", "%x%n", &u, &pos) == 1);
    CHECK(u == 16 && pos == 4);

    CHECK(sscanf("0x10zz", "%i%n", &n, &pos) == 1);
    CHECK(n == 16 && pos == 4);

    CHECK(sscanf("17x", "%o%n", &u, &pos) == 1);
    CHECK(u == 15 && pos == 2);

    CHECK(sscanf("42kg", "%u%n", &u, &pos) == 1);
    CHECK(u == 42 && pos == 2);

    CHECK(sscanf("0x", "%x%n", &u, &pos) == 1);
    CHECK(u == 0 && pos == 1);

    n   = 0;
    pos = -1;

    CHECK(sscanf("abc", "%d%n", &n, &pos) == 0);
    CHECK(n == 0 && pos == -1);

    CHECK(sscanf("12abc", "%1d%n", &n, &pos) == 1);
    CHECK(n == 1 && pos == 1);
}

static void test_float_prefixes(void) {
    float  f;
    double d;
    int    pos;

    CHECK(sscanf("3.5x", "%f%n", &f, &pos) == 1);
    CHECK(f == 3.5f && pos == 3);

    CHECK(sscanf("3.5x", "%2f%n", &f, &pos) == 1);
    CHECK(f == 3.0f && pos == 2);

    CHECK(sscanf("-2.5e3z", "%lf%n", &d, &pos) == 1);
    CHECK(d == -2500.0 && pos == 6);

    CHECK(sscanf("0x1.8p+1zz", "%lf%n", &d, &pos) == 1);
    CHECK(d == 3.0 && pos == 8);

    CHECK(sscanf("1e", "%f%n", &f, &pos) == 1);
    CHECK(f == 1.0f && pos == 1);

    CHECK(sscanf("1.2.3", "%lf%n", &d, &pos) == 1);
    CHECK(d == 1.2 && pos == 3);

    f   = 0.0f;
    pos = -1;

    CHECK(sscanf("abc", "%f%n", &f, &pos) == 0);
    CHECK(f == 0.0f && pos == -1);
}

static void test_pointer_prefix(void) {
    void* ptr = NULL;

    int pos = -1;

    CHECK(sscanf("0x10zz", "%p%n", &ptr, &pos) == 1);
    CHECK((uintptr_t) ptr == 16 && pos == 4);
}

int main(void) {
    test_integer_prefixes();
    test_float_prefixes();
    test_pointer_prefix();

    return fails != 0;
}
