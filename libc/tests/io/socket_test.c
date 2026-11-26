#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                            \
    do {                                                                            \
        if (!(condition)) {                                                         \
            fprintf(stderr, "socket check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                    \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

int main(void) {
    int fds[2] = {-1, -1};
    int value  = 0;

    socklen_t length;

    int regular;

    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == 0);

    if (fds[0] >= 0) {
        length = sizeof value;

        CHECK(getsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &value, &length) == 0);
        CHECK(length == sizeof value);
        CHECK(value > 0);

        value  = 0;
        length = 1;

        CHECK(getsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &value, &length) == 0);
        CHECK(length == 1);

        {
            static const char first[]  = "first";
            static const char second[] = "second";
            char              received[sizeof second];

            CHECK(send(fds[0], first, sizeof first, 0) == (long) sizeof first);
            CHECK(send(fds[0], second, sizeof second, 0) == (long) sizeof second);
            CHECK(recv(fds[1], received, sizeof received, 0) == (long) sizeof first);
            CHECK(__builtin_memcmp(received, first, sizeof first) == 0);
            CHECK(recv(fds[1], received, sizeof received, 0) == (long) sizeof second);
            CHECK(__builtin_memcmp(received, second, sizeof second) == 0);
        }

        length = sizeof value;
        errno  = 0;

        CHECK(getsockopt(fds[0], SOL_SOCKET, 0x7fff, &value, &length) == -1);
        CHECK(errno == ENOPROTOOPT);
    }

    length = sizeof value;
    errno  = 0;

    CHECK(getsockopt(-1, SOL_SOCKET, SO_SNDBUF, &value, &length) == -1);
    CHECK(errno == EBADF);

    regular = open("/dev/null", O_RDONLY);
    if (regular >= 0) {
        length = sizeof value;
        errno  = 0;

        CHECK(getsockopt(regular, SOL_SOCKET, SO_SNDBUF, &value, &length) == -1);
        CHECK(errno == ENOTSOCK);
        close(regular);
    }

    if (fds[0] >= 0)
        close(fds[0]);

    if (fds[1] >= 0)
        close(fds[1]);

    return failures != 0;
}
