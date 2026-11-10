#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

int main(void) {
    struct timeval zero = {0, 0};

    if (poll(NULL, 0, 0) != 0)
        return 1;
    if (select(0, NULL, NULL, NULL, &zero) != 0)
        return 2;
    {
        struct pollfd fd;
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0)
            return 6;
        fd.fd = pipe_fds[0];
        fd.events = POLLIN;
        fd.revents = 0;
        if (poll(&fd, 1, 0) != 0 || close(pipe_fds[0]) != 0 || close(pipe_fds[1]) != 0)
            return 6;
    }
    if (poll(NULL, 0, -2) != -1 || errno != EINVAL)
        return 3;
    zero.tv_usec = 1000;
    if (poll(NULL, 0, 1) != 0)
        return 4;
    if (select(0, NULL, NULL, NULL, &zero) != 0)
        return 5;
    puts("poll/select zero-fd tests ok");
    return 0;
}
