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

static void sendto_null_destination_connected(void) {
    int stream[2] = {-1, -1};
    int dgram[2]  = {-1, -1};

    static const char payload[] = "null-destination";
    char              received[sizeof payload];

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, dgram) == 0);

    if (stream[0] >= 0) {
        CHECK(sendto(stream[0], payload, sizeof payload, 0, NULL, 0) ==
              (long) sizeof payload);
        CHECK(recv(stream[1], received, sizeof received, 0) == (long) sizeof payload);
        CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);

        close(stream[0]);
        close(stream[1]);
    }

    if (dgram[0] >= 0) {
        CHECK(sendto(dgram[0], payload, sizeof payload, 0, NULL, 0) ==
              (long) sizeof payload);
        CHECK(recv(dgram[1], received, sizeof received, 0) == (long) sizeof payload);
        CHECK(__builtin_memcmp(received, payload, sizeof payload) == 0);

        close(dgram[0]);
        close(dgram[1]);
    }
}

static void sendto_null_destination_unconnected(void) {
    static const int types[] = {SOCK_STREAM, SOCK_DGRAM};

    for (int i = 0; i < 2; ++i) {
        int fd = socket(AF_UNIX, types[i], 0);
        if (fd < 0) {
            CHECK(0);

            continue;
        }

        errno = 0;

        CHECK(sendto(fd, "x", 1, 0, NULL, 0) == -1);
        CHECK(errno == ENOTCONN);

        close(fd);
    }
}

int main(void) {
    int fds[2] = {-1, -1};
    int value  = 0;

    socklen_t length;

    int regular;

    sendto_null_destination_connected();
    sendto_null_destination_unconnected();

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

    {
        int listener        = -1;
        int named_socket    = -1;
        int other_socket    = -1;
        int unnamed_socket  = -1;
        int rejected_socket = -1;
        int accepted;

        struct sockaddr_un server;
        struct sockaddr_un named;
        struct sockaddr_un other;
        struct sockaddr_un peer;

        char server_path[64];
        char named_path[64];
        char other_path[64];

        int server_written;
        int named_written;
        int other_written;

        socklen_t peer_length;

        unsigned int capacity;

        __builtin_memset(&server, 0, sizeof server);
        __builtin_memset(&named, 0, sizeof named);
        __builtin_memset(&other, 0, sizeof other);

        server_written =
            snprintf(server_path, sizeof server_path, "/tmp/astia-accept-%012lx.sock",
                     (unsigned long) (uintptr_t) &server);
        named_written =
            snprintf(named_path, sizeof named_path, "/tmp/astia-named-%012lx.sock",
                     (unsigned long) (uintptr_t) &named);
        other_written =
            snprintf(other_path, sizeof other_path, "/tmp/astia-other-%012lx.sock",
                     (unsigned long) (uintptr_t) &other);

        CHECK(server_written > 0 && server_written < (int) sizeof server_path);
        CHECK(named_written > 0 && named_written < (int) sizeof named_path);
        CHECK(other_written > 0 && other_written < (int) sizeof other_path);

        if (server_written > 0 && server_written < (int) sizeof server_path &&
            named_written > 0 && named_written < (int) sizeof named_path &&
            other_written > 0 && other_written < (int) sizeof other_path) {
            unlink(server_path);
            unlink(named_path);
            unlink(other_path);

            listener = socket(AF_UNIX, SOCK_STREAM, 0);

            CHECK(listener >= 0);

            if (listener >= 0) {
                server.sun_family = AF_UNIX;

                __builtin_memcpy(server.sun_path, server_path, server_written + 1);

                CHECK(bind(listener, (struct sockaddr*) &server, sizeof server) == 0);
                CHECK(listen(listener, 4) == 0);

                named_socket = socket(AF_UNIX, SOCK_STREAM, 0);

                CHECK(named_socket >= 0);

                if (named_socket >= 0) {
                    named.sun_family = AF_UNIX;

                    __builtin_memcpy(named.sun_path, named_path, named_written + 1);

                    CHECK(bind(named_socket, (struct sockaddr*) &named, sizeof named) ==
                          0);
                    CHECK(connect(named_socket, (struct sockaddr*) &server,
                                  sizeof server) == 0);

                    __builtin_memset(&peer, 0, sizeof peer);

                    peer_length = sizeof peer;
                    accepted = accept(listener, (struct sockaddr*) &peer, &peer_length);

                    CHECK(accepted >= 0);
                    CHECK(peer_length == (socklen_t) named_written + 3);
                    CHECK(peer.sun_family == AF_UNIX);
                    CHECK(__builtin_memcmp(peer.sun_path, named_path,
                                           named_written + 1) == 0);

                    if (accepted >= 0)
                        close(accepted);
                }

                other_socket = socket(AF_UNIX, SOCK_STREAM, 0);

                CHECK(other_socket >= 0);

                if (other_socket >= 0) {
                    other.sun_family = AF_UNIX;

                    __builtin_memcpy(other.sun_path, other_path, other_written + 1);

                    CHECK(bind(other_socket, (struct sockaddr*) &other, sizeof other) ==
                          0);
                    CHECK(connect(other_socket, (struct sockaddr*) &server,
                                  sizeof server) == 0);

                    __builtin_memset(&peer, 0xaa, sizeof peer);

                    capacity    = 8;
                    peer_length = capacity;
                    accepted = accept(listener, (struct sockaddr*) &peer, &peer_length);

                    CHECK(accepted >= 0);
                    CHECK(peer_length == (socklen_t) other_written + 3);
                    CHECK(peer.sun_family == AF_UNIX);
                    CHECK(__builtin_memcmp(peer.sun_path, other_path, 6) == 0);
                    CHECK(((unsigned char*) &peer)[8] == 0xaa);

                    if (accepted >= 0)
                        close(accepted);
                }

                unnamed_socket = socket(AF_UNIX, SOCK_STREAM, 0);

                CHECK(unnamed_socket >= 0);

                if (unnamed_socket >= 0) {
                    CHECK(connect(unnamed_socket, (struct sockaddr*) &server,
                                  sizeof server) == 0);

                    __builtin_memset(&peer, 0xaa, sizeof peer);

                    peer_length = sizeof peer;
                    accepted = accept(listener, (struct sockaddr*) &peer, &peer_length);

                    CHECK(accepted >= 0);
                    CHECK(peer_length == 2);
                    CHECK(peer.sun_family == AF_UNIX);

                    if (accepted >= 0)
                        close(accepted);
                }

                rejected_socket = socket(AF_UNIX, SOCK_STREAM, 0);

                CHECK(rejected_socket >= 0);

                if (rejected_socket >= 0) {
                    CHECK(connect(rejected_socket, (struct sockaddr*) &server,
                                  sizeof server) == 0);

                    peer_length = sizeof peer;
                    errno       = 0;

                    CHECK(accept(listener, (struct sockaddr*) &peer, NULL) == -1);
                    CHECK(errno == EFAULT);

                    accepted = accept(listener, NULL, NULL);

                    CHECK(accepted >= 0);

                    if (accepted >= 0)
                        close(accepted);
                }

                if (unnamed_socket >= 0)
                    close(unnamed_socket);

                if (other_socket >= 0)
                    close(other_socket);

                if (named_socket >= 0)
                    close(named_socket);

                if (rejected_socket >= 0)
                    close(rejected_socket);

                if (listener >= 0)
                    close(listener);
            }

            unlink(server_path);
            unlink(named_path);
            unlink(other_path);
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
