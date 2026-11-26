#include <sys/socket.h>
#include <sys/uio.h>

#include <errno.h>
#include <fcntl.h>
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

int main(void) {
    pass_descriptor();
    pass_descriptor_cloexec();
    truncated_control();
    invalid_control();

    return failures != 0;
}
