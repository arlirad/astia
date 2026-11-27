#include "internal/pty_name.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void check_name_copy(void) {
    char   exact[PTY_NAME_MAX];
    char   short_buffer[8];
    size_t i;

    CHECK(__astia_pty_copy_name(exact, sizeof exact, "/dev/pts/7") == 0);
    CHECK(strcmp(exact, "/dev/pts/7") == 0);

    CHECK(__astia_pty_copy_name(exact, 12, "/dev/pts/12") == 0);
    CHECK(__astia_pty_copy_name(exact, 11, "/dev/pts/12") == ERANGE);

    memset(short_buffer, 0x5A, sizeof short_buffer);

    CHECK(__astia_pty_copy_name(short_buffer, sizeof short_buffer, "/dev/pts/1234") ==
          ERANGE);

    for (i = 0; i < sizeof short_buffer; ++i) {
        CHECK((unsigned char) short_buffer[i] == 0x5A);
    }

    CHECK(__astia_pty_copy_name(NULL, sizeof exact, "/dev/pts/7") == EINVAL);
    CHECK(__astia_pty_copy_name(exact, sizeof exact, NULL) == EINVAL);
    CHECK(__astia_pty_copy_name(exact, 0, "/dev/pts/7") == EINVAL);
}

static void check_openpty_name(void) {
    char generated[PTY_NAME_MAX];
    char expected[PTY_NAME_MAX];
    int  master = -1;
    int  slave  = -1;

    if (openpty(&master, &slave, generated, NULL, NULL) != 0) {
        printf("SKIP host pty: %s\n", strerror(errno));

        return;
    }

    CHECK(ptsname_r(master, expected, sizeof expected) == 0);
    CHECK(strcmp(generated, expected) == 0);
    CHECK(strlen(generated) < PTY_NAME_MAX);

    if (close(slave) != 0 || close(master) != 0)
        ++failures;
}

int main(void) {
    check_name_copy();
    check_openpty_name();

    return failures != 0;
}
