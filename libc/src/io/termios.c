#include <sys/ioctl.h>

#include <errno.h>
#include <termios.h>

int tcgetattr(int fd, struct termios* termios_p) {
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int actions, const struct termios* termios_p) {
    unsigned long request;

    if (actions == TCSANOW) {
        request = TCSETS;
    }
    else if (actions == TCSADRAIN) {
        request = TCSETSW;
    }
    else if (actions == TCSAFLUSH) {
        request = TCSETSF;
    }
    else {
        errno = EINVAL;

        return -1;
    }

    return ioctl(fd, request, (void*) termios_p);
}

int tcsendbreak(int fd, int duration) {
    (void) fd;
    (void) duration;

    errno = ENOSYS;

    return -1;
}

int tcdrain(int fd) {
    (void) fd;

    errno = ENOSYS;

    return -1;
}

int tcflush(int fd, int queue_selector) {
    if (fd < 0) {
        errno = EBADF;

        return -1;
    }

    if (queue_selector != TCIFLUSH && queue_selector != TCOFLUSH &&
        queue_selector != TCIOFLUSH) {
        errno = EINVAL;

        return -1;
    }

    return ioctl(fd, TCFLSH, &queue_selector);
}

int tcflow(int fd, int action) {
    (void) fd;
    (void) action;

    errno = ENOSYS;

    return -1;
}

speed_t cfgetispeed(const struct termios* termios_p) {
    return termios_p->c_ispeed;
}

speed_t cfgetospeed(const struct termios* termios_p) {
    return termios_p->c_ospeed;
}

int cfsetispeed(struct termios* termios_p, speed_t speed) {
    termios_p->c_ispeed = speed;

    return 0;
}

int cfsetospeed(struct termios* termios_p, speed_t speed) {
    termios_p->c_ospeed = speed;

    return 0;
}
