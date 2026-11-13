#include <inttypes.h>
#include <stdio.h>
#include <sys/times.h>

static int fails;

#define CHECK(condition) \
    do { \
        if (!(condition)) \
            ++fails; \
    } while (0)

int main(void) {
    char* end;
    imaxdiv_t quotient = imaxdiv(17, 5);
    struct tms usage;
    clock_t ticks = times(&usage);
    CHECK(sizeof(intmax_t) == 8 && sizeof(uintmax_t) == 8);
    CHECK(imaxabs(-7) == 7 && quotient.quot == 3 && quotient.rem == 2);
    CHECK(strtoimax("-123x", &end, 10) == -123 && *end == 'x');
    CHECK(strtoumax("456x", &end, 10) == 456 && *end == 'x');
    CHECK(ticks >= 0 && usage.tms_utime == 0 && usage.tms_stime == 0);
    return fails != 0;
}
