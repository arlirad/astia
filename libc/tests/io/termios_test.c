#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
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
    struct termios value;

    int master   = -1;
    int slave    = -1;
    int null_fd  = open("/dev/null", O_RDONLY);
    int have_pty = open_pty_pair(&master, &slave) == 0;

    CHECK(null_fd >= 0);

    errno = 0;

    CHECK(isatty(null_fd) == 0 && errno == ENOTTY);

    errno = 0;

    CHECK(tcflush(null_fd, TCIFLUSH) == -1 && errno == ENOTTY);

    errno = 0;

    CHECK(ioctl(null_fd, 0xdeadbeefUL, NULL) == -1 && errno == ENOTTY);

    errno = 0;

    CHECK(tcflush(null_fd, 99) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(isatty(-1) == 0 && errno == EBADF);

    errno = 0;

    CHECK(tcflush(-1, TCIFLUSH) == -1 && errno == EBADF);

    errno = 0;

    CHECK(tcflush(-1, 99) == -1 && errno == EBADF);

    errno = 0;

    CHECK(ioctl(-1, TCFLSH, NULL) == -1 && errno == EBADF);

    errno = 0;

    CHECK(ioctl(-1, 0xdeadbeefUL, NULL) == -1 && errno == EBADF);

    errno = 0;

    CHECK(tcgetattr(-1, &value) == -1 && errno == EBADF);

    errno = 0;

    CHECK(tcsetattr(-1, TCSANOW, &value) == -1 && errno == EBADF);

    errno = 0;

    CHECK(tcsetattr(null_fd, 99, &value) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(tcsendbreak(null_fd, 0) == -1 && errno == ENOSYS);

    errno = 0;

    CHECK(tcdrain(null_fd) == -1 && errno == ENOSYS);

    errno = 0;

    CHECK(tcflow(null_fd, 0) == -1 && errno == ENOSYS);

    if (have_pty) {
        errno = 0;

        CHECK(isatty(slave) == 1);

        errno = 0;

        CHECK(tcflush(slave, TCIFLUSH) == 0);

        errno = 0;

        CHECK(tcflush(slave, TCOFLUSH) == 0);

        errno = 0;

        CHECK(tcflush(slave, TCIOFLUSH) == 0);

        errno = 0;

        CHECK(tcflush(slave, 99) == -1 && errno == EINVAL);

        errno = 0;

        CHECK(ioctl(slave, TCFLSH, NULL) == -1 && errno == EFAULT);

        errno = 0;

        CHECK(isatty(-1) == 0 && errno == EBADF);

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
