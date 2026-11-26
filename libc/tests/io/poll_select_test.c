#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

static int closed_descriptor_fails(int set_index) {
    struct timeval immediate = {0, 0};
    fd_set         read_set, write_set, except_set;
    fd_set*        sets[3];

    int result = -1;
    int pipe_fds[2];

    if (pipe(pipe_fds) != 0)
        return -1;

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_ZERO(&except_set);

    sets[0] = &read_set;
    sets[1] = &write_set;
    sets[2] = &except_set;

    FD_SET(pipe_fds[0], sets[set_index]);
    FD_SET(pipe_fds[1], &write_set);

    if (close(pipe_fds[0]) != 0)
        goto out;

    errno = 0;

    if (select(pipe_fds[1] + 1, &read_set, &write_set, &except_set, &immediate) == -1 &&
        errno == EBADF)
        result = 0;

out:
    close(pipe_fds[1]);

    return result;
}

static int duplicate_ready_sets_count(void) {
    struct timeval immediate = {0, 0};
    fd_set         read_set, write_set;

    int result = -1;
    int pair[2];
    int ready;

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) != 0)
        return -1;

    if (send(pair[1], "x", 1, 0) != 1)
        goto out;

    FD_ZERO(&read_set);
    FD_ZERO(&write_set);
    FD_SET(pair[0], &read_set);
    FD_SET(pair[0], &write_set);

    ready = select(pair[0] + 1, &read_set, &write_set, NULL, &immediate);

    if (ready == 2 && FD_ISSET(pair[0], &read_set) && FD_ISSET(pair[0], &write_set))
        result = 0;

out:
    close(pair[0]);
    close(pair[1]);

    return result;
}

int main(void) {
    struct timeval zero = {0, 0};

    if (poll(NULL, 0, 0) != 0)
        return 1;

    if (select(0, NULL, NULL, NULL, &zero) != 0)
        return 2;

    if (duplicate_ready_sets_count() != 0)
        return 7;

    for (int set_index = 0; set_index < 3; ++set_index) {
        if (closed_descriptor_fails(set_index) != 0)
            return 8;
    }

    {
        struct pollfd fd;

        int pipe_fds[2];

        if (pipe(pipe_fds) != 0)
            return 6;

        fd.fd      = pipe_fds[0];
        fd.events  = POLLIN;
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
