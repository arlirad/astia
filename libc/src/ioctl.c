#include "file_port.h"
#include <sys/ioctl.h>

#include <errno.h>
#include <stdarg.h>
#include <termios.h>

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void*   arg;

    size_t length = 0;
    long   rc;

    if (fd < 0) {
        errno = EBADF;

        return -1;
    }

    if (request == TIOCSCTTY) {
        arg = NULL;
    }
    else {
        va_start(ap, request);

        arg = va_arg(ap, void*);

        va_end(ap);
    }

    if (request == TCGETS || request == TCSETS || request == TCSETSW ||
        request == TCSETSF) {
        length = sizeof(struct termios);
    }
    else if (request == TCFLSH) {
        length = sizeof(int);
    }
    else if (request == TIOCGWINSZ || request == TIOCSWINSZ) {
        length = sizeof(struct winsize);
    }
    else if (request == TIOCGPGRP || request == TIOCSPGRP) {
        length = sizeof(int);
    }
    else if (request == TIOCGPTN || request == TIOCSPTLCK || request == TIOCGPTLCK) {
        length = sizeof(int);
    }
    else if (request == TIOCSCTTY) {
        length = 0;
    }
    else {
        errno = ENOTTY;

        return -1;
    }

    rc = __astia_fio_ioctl(fd, request, arg, length);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return (int) rc;
}
