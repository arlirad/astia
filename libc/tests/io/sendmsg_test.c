#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                             \
    do {                                                                             \
        if (!(condition)) {                                                          \
            fprintf(stderr, "sendmsg check failed: %s:%d: %s\n", __FILE__, __LINE__, \
                    #condition);                                                     \
            ++failures;                                                              \
        }                                                                            \
    } while (0)

static int new_pair(int fds[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return -1;

    return 0;
}

static struct cmsghdr* rights_header(void* buffer, int fd) {
    struct cmsghdr* header = (struct cmsghdr*) buffer;

    header->cmsg_len   = CMSG_LEN(sizeof(int));
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type  = SCM_RIGHTS;

    memcpy(CMSG_DATA(header), &fd, sizeof fd);

    return header;
}

static void pass_descriptor(void) {
    int pair[2] = {-1, -1};
    int pipe_fds[2];

    if (new_pair(pair) != 0 || pipe(pipe_fds) != 0) {
        CHECK(0);

        return;
    }

    if (write(pipe_fds[1], "z", 1) != 1) {
        CHECK(0);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        close(pair[0]);
        close(pair[1]);

        return;
    }

    char control[CMSG_SPACE(sizeof(int))];
    char data = 'x';

    struct iovec send_iov = {&data, 1};

    struct msghdr outgoing = {0};

    outgoing.msg_iov        = &send_iov;
    outgoing.msg_iovlen     = 1;
    outgoing.msg_control    = control;
    outgoing.msg_controllen = sizeof control;

    rights_header(control, pipe_fds[0]);

    CHECK(sendmsg(pair[0], &outgoing, 0) == 1);

    close(pipe_fds[0]);

    char received_data[2] = {0};
    char received_control[CMSG_SPACE(sizeof(int))];

    struct iovec  receive_iov = {received_data, 1};
    struct msghdr incoming    = {0};

    incoming.msg_iov        = &receive_iov;
    incoming.msg_iovlen     = 1;
    incoming.msg_control    = received_control;
    incoming.msg_controllen = sizeof received_control;

    const long received = recvmsg(pair[1], &incoming, 0);

    CHECK(received == 1);
    CHECK(received_data[0] == 'x');
    CHECK(incoming.msg_flags == 0);

    struct cmsghdr* header = CMSG_FIRSTHDR(&incoming);

    CHECK(header != NULL);

    if (header != NULL) {
        int received_fd = -1;

        CHECK(header->cmsg_level == SOL_SOCKET);
        CHECK(header->cmsg_type == SCM_RIGHTS);
        CHECK(header->cmsg_len == CMSG_LEN(sizeof(int)));

        memcpy(&received_fd, CMSG_DATA(header), sizeof received_fd);

        CHECK(received_fd >= 0);
        CHECK(fcntl(received_fd, F_GETFD) == 0);

        char byte = 0;

        CHECK(read(received_fd, &byte, 1) == 1);
        CHECK(byte == 'z');

        close(received_fd);
    }

    close(pipe_fds[1]);
    close(pair[0]);
    close(pair[1]);
}

static void pass_descriptor_cloexec(void) {
    int pair[2] = {-1, -1};
    int pipe_fds[2];

    if (new_pair(pair) != 0 || pipe(pipe_fds) != 0) {
        CHECK(0);

        return;
    }

    char control[CMSG_SPACE(sizeof(int))];
    char data = 'c';

    struct iovec  send_iov = {&data, 1};
    struct msghdr outgoing = {0};

    outgoing.msg_iov        = &send_iov;
    outgoing.msg_iovlen     = 1;
    outgoing.msg_control    = control;
    outgoing.msg_controllen = sizeof control;

    rights_header(control, pipe_fds[0]);

    CHECK(sendmsg(pair[0], &outgoing, 0) == 1);
    close(pipe_fds[0]);

    char received_data = 0;
    char received_control[CMSG_SPACE(sizeof(int))];

    struct iovec  receive_iov = {&received_data, 1};
    struct msghdr incoming    = {0};

    incoming.msg_iov        = &receive_iov;
    incoming.msg_iovlen     = 1;
    incoming.msg_control    = received_control;
    incoming.msg_controllen = sizeof received_control;

    CHECK(recvmsg(pair[1], &incoming, MSG_CMSG_CLOEXEC) == 1);

    struct cmsghdr* header = CMSG_FIRSTHDR(&incoming);

    int received_fd = -1;

    CHECK(header != NULL);

    if (header != NULL) {
        memcpy(&received_fd, CMSG_DATA(header), sizeof received_fd);

        CHECK(received_fd >= 0);
        CHECK((fcntl(received_fd, F_GETFD) & FD_CLOEXEC) != 0);

        close(received_fd);
    }

    close(pipe_fds[1]);
    close(pair[0]);
    close(pair[1]);
}

static void truncated_control(void) {
    int pair[2] = {-1, -1};
    int pipe_fds[2];

    if (new_pair(pair) != 0 || pipe(pipe_fds) != 0) {
        CHECK(0);

        return;
    }

    char control[CMSG_SPACE(sizeof(int))];
    char data = 't';

    struct iovec  send_iov = {&data, 1};
    struct msghdr outgoing = {0};

    outgoing.msg_iov        = &send_iov;
    outgoing.msg_iovlen     = 1;
    outgoing.msg_control    = control;
    outgoing.msg_controllen = sizeof control;

    rights_header(control, pipe_fds[0]);

    CHECK(sendmsg(pair[0], &outgoing, 0) == 1);
    close(pipe_fds[0]);

    char received_data                         = 0;
    char small_control[sizeof(struct cmsghdr)] = {0};

    struct iovec  receive_iov = {&received_data, 1};
    struct msghdr incoming    = {0};

    incoming.msg_iov        = &receive_iov;
    incoming.msg_iovlen     = 1;
    incoming.msg_control    = small_control;
    incoming.msg_controllen = sizeof small_control;

    CHECK(recvmsg(pair[1], &incoming, 0) == 1);
    CHECK((incoming.msg_flags & MSG_CTRUNC) != 0);

    close(pipe_fds[1]);
    close(pair[0]);
    close(pair[1]);
}

static void invalid_control(void) {
    int pair[2] = {-1, -1};

    if (new_pair(pair) != 0) {
        CHECK(0);

        return;
    }

    char control[CMSG_SPACE(sizeof(int))] = {0};
    char data                             = 'b';

    struct iovec  send_iov = {&data, 1};
    struct msghdr outgoing = {0};

    outgoing.msg_iov        = &send_iov;
    outgoing.msg_iovlen     = 1;
    outgoing.msg_control    = control;
    outgoing.msg_controllen = sizeof control;

    ((struct cmsghdr*) control)->cmsg_len   = sizeof(struct cmsghdr) - 1;
    ((struct cmsghdr*) control)->cmsg_level = SOL_SOCKET;
    ((struct cmsghdr*) control)->cmsg_type  = SCM_RIGHTS;

    errno = 0;

    CHECK(sendmsg(pair[0], &outgoing, 0) == -1);
    CHECK(errno == EINVAL);

    close(pair[0]);
    close(pair[1]);
}

static void datagram_named_destination(void) {
    int sender   = socket(AF_UNIX, SOCK_DGRAM, 0);
    int receiver = socket(AF_UNIX, SOCK_DGRAM, 0);
    int written;

    struct sockaddr_un address;

    char path[64];

    if (sender < 0 || receiver < 0) {
        CHECK(0);

        if (sender >= 0)
            close(sender);

        if (receiver >= 0)
            close(receiver);

        return;
    }

    memset(&address, 0, sizeof address);

    address.sun_family = AF_UNIX;

    written = snprintf(path, sizeof path, "/tmp/astia-sendmsg-%012lx.sock",
                       (unsigned long) (uintptr_t) &address);

    if (written <= 0 || written >= (int) sizeof path) {
        CHECK(0);
        close(sender);
        close(receiver);

        return;
    }

    memcpy(address.sun_path, path, (unsigned long) written + 1);

    unlink(path);
    CHECK(bind(receiver, (struct sockaddr*) &address, sizeof address) == 0);

    static const char first[]  = "named-";
    static const char second[] = "destination";

    char received[sizeof first + sizeof second];

    struct iovec iov[2] = {{(void*) first, sizeof first},
                           {(void*) second, sizeof second}};

    struct msghdr outgoing = {0};

    outgoing.msg_name    = &address;
    outgoing.msg_namelen = sizeof address;
    outgoing.msg_iov     = iov;
    outgoing.msg_iovlen  = 2;

    CHECK(sendmsg(sender, &outgoing, 0) == (long) (sizeof first + sizeof second));
    CHECK(recv(receiver, received, sizeof received, 0) ==
          (long) (sizeof first + sizeof second));
    CHECK(memcmp(received, first, sizeof first) == 0);
    CHECK(memcmp(received + sizeof first, second, sizeof second) == 0);

    unlink(path);
    close(sender);
    close(receiver);
}

static void datagram_missing_destination(void) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);

    char data = 'x';

    struct iovec  iov      = {&data, 1};
    struct msghdr outgoing = {0};

    if (fd < 0) {
        CHECK(0);

        return;
    }

    outgoing.msg_iov    = &iov;
    outgoing.msg_iovlen = 1;

    errno = 0;

    CHECK(sendmsg(fd, &outgoing, 0) == -1);
    CHECK(errno == ENOTCONN);

    close(fd);
}

static void datagram_invalid_destination_length(void) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);

    struct sockaddr_un address;

    char data = 'x';

    struct iovec  iov      = {&data, 1};
    struct msghdr outgoing = {0};

    unsigned int lengths[] = {0, 2, sizeof address + 1};

    if (fd < 0) {
        CHECK(0);

        return;
    }

    memset(&address, 0, sizeof address);

    address.sun_family = AF_UNIX;

    outgoing.msg_name   = &address;
    outgoing.msg_iov    = &iov;
    outgoing.msg_iovlen = 1;

    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; ++i) {
        outgoing.msg_namelen = lengths[i];

        errno = 0;

        CHECK(sendmsg(fd, &outgoing, 0) == -1);
        CHECK(errno == EINVAL);
    }

    close(fd);
}

static void datagram_connected_null_destination(void) {
    int pair[2] = {-1, -1};

    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) != 0) {
        CHECK(0);

        return;
    }

    static const char payload[] = "connected";

    char received[sizeof payload];

    struct iovec  iov      = {(void*) payload, sizeof payload};
    struct msghdr outgoing = {0};

    outgoing.msg_name    = NULL;
    outgoing.msg_namelen = sizeof(struct sockaddr_un);
    outgoing.msg_iov     = &iov;
    outgoing.msg_iovlen  = 1;

    CHECK(sendmsg(pair[0], &outgoing, 0) == (long) sizeof payload);
    CHECK(recv(pair[1], received, sizeof received, 0) == (long) sizeof payload);
    CHECK(memcmp(received, payload, sizeof payload) == 0);

    close(pair[0]);
    close(pair[1]);
}

int main(void) {
    pass_descriptor();
    pass_descriptor_cloexec();
    truncated_control();
    invalid_control();
    datagram_named_destination();
    datagram_missing_destination();
    datagram_invalid_destination_length();
    datagram_connected_null_destination();

    return failures != 0;
}
