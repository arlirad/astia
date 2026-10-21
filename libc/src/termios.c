#include <sys/ioctl.h>

#include <errno.h>
#include <termios.h>

int tcgetattr(int fd, struct termios* termios_p) {
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int actions, const struct termios* termios_p) {
    (void) actions;
    return ioctl(fd, TCSETS, (void*) termios_p);
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
    (void) fd;
    (void) queue_selector;
    errno = ENOSYS;
    return -1;
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
