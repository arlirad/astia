#include "../../libc/include/daemon_protocol.h"
#include "common.h"
#include "daemonctl_path.h"
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static int command_errno(const char* name, const char* detail) {
    char message[96];

    snprintf(message, sizeof message, "%s (errno=%d)", detail, errno);

    return cu_error(name, message);
}

static int connect_daemon(int fd, const char* path) {
    struct sockaddr_un address;

    size_t path_length;

    if (cu_daemonctl_socket_address(&address, path, &path_length) != 0)
        return -1;

    return connect(
        fd, (struct sockaddr*) &address,
        (unsigned int) (offsetof(struct sockaddr_un, sun_path) + path_length + 1));
}

static int command(const char* name, const char* request) {
    char   response[DAEMON_RESPONSE_MAX];
    int    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    long   n;
    size_t length = __builtin_strlen(request);

    if (fd < 0)
        return command_errno(name, "daemon unavailable");

    if (connect_daemon(fd, DAEMON_SOCKET_PATH) < 0 &&
        connect_daemon(fd, DAEMON_SOCKET_FALLBACK) < 0) {
        close(fd);

        return command_errno(name, "daemon unavailable");
    }

    {
        const char* cursor = request;

        size_t remaining = length;

        while (remaining) {
            long written = send(fd, cursor, (unsigned long) remaining, 0);
            if (written <= 0) {
                close(fd);

                return command_errno(name, "daemon request failed");
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
            close(fd);

            return command_errno(name, "daemon response failed");
        }

        if (received == 0)
            break;

        n += received;
    }

    close(fd);

    if (n <= 0)
        return command_errno(name, "daemon response failed");

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
