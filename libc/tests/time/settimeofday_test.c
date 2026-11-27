#include <sys/time.h>

#include <errno.h>
#include <stdio.h>

static int fails;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

int main(void) {
    struct timeval  value = {1767225600, 0};
    struct timezone zone  = {0, 0};

    errno = 0;

    CHECK(settimeofday(NULL, &zone) == -1 && errno == EINVAL);

    value.tv_usec = 1000000;

    errno = 0;

    CHECK(settimeofday(&value, NULL) == -1 && errno == EINVAL);

    value.tv_usec = -1;

    errno = 0;

    CHECK(settimeofday(&value, NULL) == -1 && errno == EINVAL);

    value.tv_usec = 999999;

    errno = 0;

    CHECK(settimeofday(&value, &zone) == -1 && errno == ENOSYS);

    puts("settimeofday tests ok");

    return fails != 0;
}
