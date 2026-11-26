#include <errno.h>
#include <fcntl.h>
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

static int open_pty_pair(int* master, int* slave) {
    char name[64];

    *master = posix_openpt(O_RDWR | O_NOCTTY);

    if (*master < 0 || grantpt(*master) != 0 || unlockpt(*master) != 0 ||
        ptsname_r(*master, name, sizeof name) != 0)
        return -1;

    *slave = open(name, O_RDWR | O_NOCTTY);

    return *slave < 0 ? -1 : 0;
}

int main(void) {
    char master_name[64];
    char slave_name[64];

    int master   = -1;
    int slave    = -1;
    int null_fd  = open("/dev/null", O_RDONLY);
    int have_pty = open_pty_pair(&master, &slave) == 0;

    CHECK(null_fd >= 0);

    errno = 0;

    CHECK(ttyname_r(null_fd, slave_name, sizeof slave_name) == ENOTTY);

    errno = 0;

    CHECK(ttyname(-1) == NULL && errno == EBADF);

    errno = 0;

    CHECK(ttyname_r(-1, slave_name, sizeof slave_name) == EBADF);

    errno = 0;

    CHECK(ttyname_r(null_fd, NULL, sizeof slave_name) == EINVAL);

    if (have_pty) {
        CHECK(ptsname_r(master, master_name, sizeof master_name) == 0);

        errno = 0;

        CHECK(ttyname_r(slave, slave_name, sizeof slave_name) == 0);

        CHECK(strcmp(slave_name, master_name) == 0);

        CHECK(ttyname(slave) != NULL && strcmp(ttyname(slave), master_name) == 0);

        errno = 0;

        CHECK(ttyname_r(slave, slave_name, strlen(master_name)) == ERANGE);

        if (close(slave) != 0 || close(master) != 0)
            ++failures;
    }
    else {
        printf("SKIP host pty: %s\n", strerror(errno));
    }

    if (close(null_fd) != 0)
        ++failures;

    return failures != 0;
}
