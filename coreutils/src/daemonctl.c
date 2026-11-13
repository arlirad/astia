#include "../../libc/include/daemon_protocol.h"
#include "common.h"
#include <sys/socket.h>
#include <sys/un.h>

#include <errno.h>
#include <unistd.h>

static int connect_daemon(int fd, const char* path) {
    struct sockaddr_un address;
    __builtin_memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    __builtin_strcpy(address.sun_path, path);
    return connect(fd, (struct sockaddr*) &address,
                   (unsigned int) (2 + __builtin_strlen(path)));
}

static int command(const char* name, const char* request) {
    char   response[DAEMON_RESPONSE_MAX];
    int    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    long   n;
    size_t length = __builtin_strlen(request);
    if (fd < 0)
        return cu_error(name, "daemon unavailable");
    if (connect_daemon(fd, DAEMON_SOCKET_PATH) < 0 &&
        connect_daemon(fd, DAEMON_SOCKET_FALLBACK) < 0) {
        close(fd);
        return cu_error(name, "daemon unavailable");
    }
    {
        const char* cursor    = request;
        size_t      remaining = length;
        while (remaining) {
            long written = send(fd, cursor, (unsigned long) remaining, 0);
            if (written <= 0) {
                close(fd);
                return cu_error(name, "daemon request failed");
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
            return cu_error(name, "daemon response failed");
        }
        if (received == 0)
            break;
        n += received;
    }
    close(fd);
    if (n <= 0)
        return cu_error(name, "daemon response failed");
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
