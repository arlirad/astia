#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static int fails;

#define CHECK(condition)                                                   \
    do {                                                                    \
        if (!(condition)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++fails;                                                        \
        }                                                                   \
    } while (0)

int main(void) {
    const int pid = getpid();
    const int original_group = getpgrp();

    CHECK(pid > 0);
    CHECK(original_group > 0);
    CHECK(getpgid(0) == original_group);
    CHECK(getpgid(pid) == original_group);

    errno = 0;
    CHECK(getpgid(-1) == -1 && errno == ESRCH);

    CHECK(setpgrp() == 0);
    CHECK(getpgrp() == pid);
    CHECK(getpgid(pid) == pid);
    CHECK(setpgid(0, original_group) == 0);
    CHECK(getpgrp() == original_group);
    return fails != 0;
}
