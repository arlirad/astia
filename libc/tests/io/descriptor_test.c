#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    int fds[2];

    if (pipe(fds) != 0)
        return 1;

    if (fcntl(fds[0], F_GETFD) != 0)
        return 2;

    if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(fds[0], F_GETFD) != FD_CLOEXEC)
        return 3;

    int copy = fcntl(fds[0], F_DUPFD, fds[1] + 1);

    if (copy < fds[1] + 1 || fcntl(copy, F_GETFD) != 0)
        return 4;

    int cloexec = fcntl(fds[0], F_DUPFD_CLOEXEC, copy + 1);

    if (cloexec < copy + 1 || fcntl(cloexec, F_GETFD) != FD_CLOEXEC)
        return 5;

    int flags = fcntl(fds[0], F_GETFL);

    if ((flags & O_NONBLOCK) != 0 || fcntl(fds[0], F_SETFL, flags | O_NONBLOCK) != 0 ||
        (fcntl(fds[0], F_GETFL) & O_NONBLOCK) == 0)
        return 6;

    close(cloexec);
    close(copy);
    close(fds[0]);
    close(fds[1]);

    return 0;
}
