#include <sys/select.h>
#include <sys/time.h>

#include <errno.h>
#include <stdlib.h>

enum {
    max_timeout_ms = 2147483647,
    ms_per_sec     = 1000,
    usec_per_sec   = 1000000,
};

int select(int count, fd_set* read_set, fd_set* write_set, fd_set* except_set,
           struct timeval* timeout) {
    struct pollfd* pfds;

    int n = 0, rc, timeout_ms = -1;

    if (count < 0 || count > FD_SETSIZE ||
        (timeout != NULL && (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
                             timeout->tv_usec >= usec_per_sec))) {
        errno = EINVAL;

        return -1;
    }

    if (timeout != NULL) {
        /* Saturate before multiplying so a large timeval cannot overflow. */
        if (timeout->tv_sec > (max_timeout_ms - (ms_per_sec - 1)) / ms_per_sec) {
            timeout_ms = max_timeout_ms;
        }
        else {
            long long ms = (long long) timeout->tv_sec * ms_per_sec +
                           (timeout->tv_usec + ms_per_sec - 1) / ms_per_sec;

            timeout_ms = (int) ms;
        }
    }

    pfds = (struct pollfd*) malloc((size_t) count * sizeof(*pfds));

    if (pfds == NULL && count != 0) {
        errno = ENOMEM;

        return -1;
    }

    for (int fd = 0; fd < count; ++fd) {
        short events = 0;

        if (read_set != NULL && FD_ISSET(fd, read_set))
            events |= POLLIN;

        if (write_set != NULL && FD_ISSET(fd, write_set))
            events |= POLLOUT;

        if (except_set != NULL && FD_ISSET(fd, except_set))
            events |= POLLPRI;

        if (events) {
            pfds[n].fd      = fd;
            pfds[n].events  = events;
            pfds[n].revents = 0;

            ++n;
        }
    }

    rc = poll(pfds, (size_t) n, timeout_ms);
    if (rc < 0) {
        free(pfds);

        return -1;
    }

    if (read_set)
        FD_ZERO(read_set);

    if (write_set)
        FD_ZERO(write_set);

    if (except_set)
        FD_ZERO(except_set);

    rc = 0;

    for (int i = 0; i < n; ++i) {
        short re = pfds[i].revents;

        if (read_set && (re & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) {
            FD_SET(pfds[i].fd, read_set);

            ++rc;
        }

        if (write_set && (re & (POLLOUT | POLLERR | POLLNVAL))) {
            FD_SET(pfds[i].fd, write_set);

            ++rc;
        }

        if (except_set && (re & (POLLPRI | POLLERR | POLLNVAL))) {
            FD_SET(pfds[i].fd, except_set);

            ++rc;
        }
    }

    free(pfds);

    return rc;
}
