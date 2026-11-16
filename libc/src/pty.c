#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int posix_openpt(int flags) {
    return open("/dev/ptmx", flags);
}

int grantpt(int fd) {
    int number;
    if (ioctl(fd, TIOCGPTN, &number) < 0)
        return -1;
    return 0;
}

int unlockpt(int fd) {
    int lock = 0;
    return ioctl(fd, TIOCSPTLCK, &lock);
}

int ptsname_r(int fd, char* buffer, unsigned long buffer_size) {
    int number;
    int length;
    if (buffer == NULL || buffer_size == 0)
        return EINVAL;
    if (ioctl(fd, TIOCGPTN, &number) < 0)
        return errno;
    length = snprintf(buffer, buffer_size, "/dev/pts/%d", number);
    if (length < 0 || (unsigned long) length + 1 > buffer_size)
        return ERANGE;
    return 0;
}

char* ptsname(int fd) {
    static _Thread_local char buffer[32];
    int rc = ptsname_r(fd, buffer, sizeof buffer);
    if (rc != 0) {
        errno = rc;
        return NULL;
    }
    return buffer;
}

int openpty(int* master, int* slave, char* name, const struct termios* termp,
            const struct winsize* winp) {
    int master_fd;
    int slave_fd;
    const char* slave_name;
    if (master == NULL || slave == NULL) {
        errno = EINVAL;
        return -1;
    }
    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0)
        return -1;
    if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
        close(master_fd);
        return -1;
    }
    slave_name = ptsname(master_fd);
    if (slave_name == NULL) {
        close(master_fd);
        return -1;
    }
    slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd < 0) {
        close(master_fd);
        return -1;
    }
    if (name != NULL)
        strcpy(name, slave_name);
    if (termp != NULL && tcsetattr(slave_fd, TCSANOW, termp) < 0)
        goto fail;
    if (winp != NULL && ioctl(slave_fd, TIOCSWINSZ, winp) < 0)
        goto fail;
    *master = master_fd;
    *slave = slave_fd;
    return 0;
fail:
    close(slave_fd);
    close(master_fd);
    return -1;
}
