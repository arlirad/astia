#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
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

    {
        int rf[2] = {-1, -1};

        CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, rf) == 0);

        if (rf[0] >= 0) {
            struct sockaddr_un bound;

            char peer_path[64];
            int  written;

            __builtin_memset(&bound, 0, sizeof bound);

            written = snprintf(peer_path, sizeof peer_path, "/tmp/astia-rf-%012lx.sock",
                               (unsigned long) (uintptr_t) &bound);

            CHECK(written > 0 && written < (int) sizeof peer_path);

            if (written > 0 && written < (int) sizeof peer_path) {
                bound.sun_family = AF_UNIX;

                __builtin_memcpy(bound.sun_path, peer_path, (unsigned long) written + 1);

                unlink(peer_path);
                CHECK(bind(rf[0], (struct sockaddr*) &bound, sizeof bound) == 0);

                {
                    static const char payload[] = "full-address";
                    char              received[sizeof payload];

                    struct sockaddr_un from;
                    socklen_t          from_len = sizeof from;

                    __builtin_memset(&from, 0, sizeof from);

                    CHECK(send(rf[0], payload, sizeof payload, 0) ==
                          (long) sizeof payload);
                    CHECK(recvfrom(rf[1], received, sizeof received, 0,
                                   (struct sockaddr*) &from,
                                   &from_len) == (long) sizeof payload);
                    CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);
                    CHECK(from_len == (socklen_t) written + 3);
                    CHECK(from.sun_family == AF_UNIX);
                    CHECK(__builtin_memcmp(from.sun_path, peer_path,
                                           (unsigned long) written + 1) == 0);
                }

                {
                    static const char payload[] = "truncated";
                    char              received[sizeof payload];

                    struct sockaddr from;
                    socklen_t       from_len = sizeof from;

                    __builtin_memset(&from, 0, sizeof from);

                    CHECK(send(rf[0], payload, sizeof payload, 0) ==
                          (long) sizeof payload);
                    CHECK(recvfrom(rf[1], received, sizeof received, 0, &from,
                                   &from_len) == (long) sizeof payload);
                    CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);
                    CHECK(from_len == sizeof from);
                    CHECK(from.sa_family == AF_UNIX);
                    CHECK(__builtin_memcmp(from.sa_data, peer_path,
                                           sizeof from.sa_data) == 0);
                }

                {
                    static const char payload[] = "not-consumed";
                    char              received[sizeof payload];

                    struct sockaddr_un from;
                    socklen_t          from_len = 1;

                    __builtin_memset(&from, 0, sizeof from);

                    CHECK(send(rf[0], payload, sizeof payload, 0) ==
                          (long) sizeof payload);

                    errno = 0;

                    CHECK(recvfrom(rf[1], received, sizeof received, 0,
                                   (struct sockaddr*) &from, &from_len) == -1);
                    CHECK(errno == EINVAL);
                    CHECK(from_len == 1);

                    from_len = sizeof from;

                    CHECK(recvfrom(rf[1], received, sizeof received, 0,
                                   (struct sockaddr*) &from,
                                   &from_len) == (long) sizeof payload);
                    CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);
                }

                {
                    static const char payload[] = "missing-length";
                    char              received[sizeof payload];

                    struct sockaddr_un from;

                    __builtin_memset(&from, 0, sizeof from);

                    CHECK(send(rf[0], payload, sizeof payload, 0) ==
                          (long) sizeof payload);

                    errno = 0;

                    CHECK(recvfrom(rf[1], received, sizeof received, 0,
                                   (struct sockaddr*) &from, NULL) == -1);
                    CHECK(errno == EFAULT);
                    CHECK(recvfrom(rf[1], received, sizeof received, 0, NULL, NULL) ==
                          (long) sizeof payload);
                    CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);
                }

                unlink(peer_path);
            }

            close(rf[0]);
            close(rf[1]);
        }
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
