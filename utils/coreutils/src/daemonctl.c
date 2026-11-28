#include "../../../libc/include/daemon_protocol.h"
#include "common.h"
#include "daemonctl_path.h"
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* The supervisor publishes a service only after its control socket is
 * listening, but a service can still lose the race against the socket
 * becoming connectable.  Retry that short transient window instead of
 * reporting a hard failure; the bound is short enough that a genuinely
 * absent daemon still fails promptly. */
enum { CONNECT_ATTEMPTS = 80, CONNECT_INTERVAL_NSEC = 25 * 1000 * 1000 };

_Static_assert(sizeof(DAEMON_SOCKET_PATH) - 1 <
                   sizeof(((struct sockaddr_un*) 0)->sun_path),
               "primary daemon socket path must fit in sun_path");
_Static_assert(sizeof(DAEMON_SOCKET_FALLBACK) - 1 <
                   sizeof(((struct sockaddr_un*) 0)->sun_path),
               "fallback daemon socket path must fit in sun_path");

static int command_errno(const char* name, const char* detail, int error) {
    char message[96];

    snprintf(message, sizeof message, "%s (errno=%d)", detail, error);

    return cu_error(name, message);
}

static int transient_connect_error(int error) {
    return error == ENOENT || error == ECONNREFUSED || error == EAGAIN;
}

/* Every attempt owns its descriptor: POSIX leaves the socket state
 * unspecified after a failed connect(), so it cannot be reused. */
static int connect_path(const char* path, int* error) {
    struct sockaddr_un address;

    size_t path_length;

    unsigned int length;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0) {
        *error = errno;

        return -1;
    }

    if (cu_daemonctl_socket_address(&address, path, &path_length) != 0) {
        *error = errno;

        close(fd);

        return -1;
    }

    /* The kernel decodes a.len - 2 path bytes and terminates the result
     * itself, so len counts the path only: counting the trailing NUL here
     * puts an embedded NUL in the payload, which a strict decoder rejects. */
    length = (unsigned int) (offsetof(struct sockaddr_un, sun_path) + path_length);

    if (connect(fd, (struct sockaddr*) &address, length) == 0)
        return fd;

    *error = errno;

    close(fd);

    return -1;
}

static int connect_daemon(int* last_error) {
    static const char* const paths[] = {DAEMON_SOCKET_PATH, DAEMON_SOCKET_FALLBACK};

    int error = ECONNREFUSED;

    for (int attempt = 0; attempt < CONNECT_ATTEMPTS; ++attempt) {
        int retry = 1;

        for (unsigned int index = 0; index < sizeof paths / sizeof paths[0]; ++index) {
            int attempt_error = 0;

            int fd = connect_path(paths[index], &attempt_error);
            if (fd >= 0)
                return fd;

            error = attempt_error;

            if (!transient_connect_error(attempt_error))
                retry = 0;
        }

        if (!retry)
            break;

        if (attempt + 1 < CONNECT_ATTEMPTS) {
            struct timespec pause = {0, CONNECT_INTERVAL_NSEC};

            while (nanosleep(&pause, NULL) != 0 && errno == EINTR) {
            }
        }
    }

    *last_error = error;

    return -1;
}

static int command(const char* name, const char* request) {
    char   response[DAEMON_RESPONSE_MAX];
    int    error = 0;
    long   n;
    size_t length = __builtin_strlen(request);
    int    fd     = connect_daemon(&error);

    if (fd < 0)
        return command_errno(name, "daemon unavailable", error);

    {
        const char* cursor = request;

        size_t remaining = length;

        while (remaining) {
            long written = send(fd, cursor, (unsigned long) remaining, 0);
            if (written <= 0) {
                error = written == 0 ? EPIPE : errno;

                close(fd);

                return command_errno(name, "daemon request failed", error);
            }

            cursor += written;
            remaining -= (size_t) written;
        }
    }

    n = 0;

    while (n < (long) sizeof response - 1) {
        long received =
            recv(fd, response + n, sizeof response - 1 - (unsigned long) n, 0);
        if (received < 0) {
            error = errno;

            close(fd);

            return command_errno(name, "daemon response failed", error);
        }

        if (received == 0)
            break;

        n += received;
    }

    close(fd);

    if (n <= 0)
        return command_errno(name, "daemon response failed", ECONNRESET);

    response[n] = '\0';

    if (__builtin_strncmp(response, "OK ", 3) != 0 &&
        __builtin_strncmp(response, "OK\n", 3) != 0) {
        cu_puts_fd(CU_STDERR, response + 4 < response + n ? response + 4 : response);

        return 1;
    }

    if (n > 3)
        cu_write_all(CU_STDOUT, response + 3, (size_t) n - 3);

    return 0;
}

int main(int argc, char** argv) {
    const char* request = 0;

    if (argc == 2 && __builtin_strcmp(argv[1], "status") == 0)
        request = "STATUS\n";
    else if (argc == 2 && __builtin_strcmp(argv[1], "start") == 0)
        request = "START\n";
    else if (argc == 2 && __builtin_strcmp(argv[1], "stop") == 0)
        request = "STOP\n";
    else if (argc == 2 && __builtin_strcmp(argv[1], "restart") == 0)
        request = "RESTART\n";
    else if (argc == 2 && __builtin_strcmp(argv[1], "validate") == 0)
        request = "CONFIG VALIDATE\n";
    else if (argc == 2 && __builtin_strcmp(argv[1], "list") == 0)
        request = "CONFIG LIST\n";
    else if (argc == 3 && __builtin_strcmp(argv[1], "config") == 0 &&
             __builtin_strcmp(argv[2], "validate") == 0)
        request = "CONFIG VALIDATE\n";
    else if (argc == 3 && __builtin_strcmp(argv[1], "config") == 0 &&
             __builtin_strcmp(argv[2], "list") == 0)
        request = "CONFIG LIST\n";
    else
        return cu_usage("daemonctl", "status|start|stop|restart|validate|list");

    return command("daemonctl", request);
}
