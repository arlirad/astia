#include "daemond.h"

#include "stop.h"
#include "units.h"
#include <daemon_protocol.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    SERVICE_MAX                = 32,
    SERVICE_STOP_GRACE_MS      = 2000,
    SERVICE_STOP_KILL_MS       = 1000,
    CONTROL_REQUEST_TIMEOUT_MS = 5000
};

struct service {
    struct unit unit;
    int         pid, pgid, restarts, enabled, start_on_exit, last_error;
};

enum command_kind {
    CMD_STATUS,
    CMD_START,
    CMD_STOP,
    CMD_RESTART,
    CMD_VALIDATE,
    CMD_LIST
};

struct command {
    int kind;
};

static volatile sig_atomic_t shutdown_pending;

static const char* bound_control_path;

static void terminate_signal(int signal_number) {
    (void) signal_number;

    shutdown_pending = 1;
}

static int start_service(struct service* service) {
    if (service->pid >= 0)
        return 0;

    struct unit* unit = &service->unit;

    int directory = -1;

    if (unit->working_directory[0]) {
        directory = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) {
            service->last_error = errno;

            return -1;
        }

        if (chdir(unit->working_directory) < 0) {
            const int error = errno;

            close(directory);

            service->last_error = error;

            errno = error;

            return -1;
        }
    }

    service->pid  = spawnve(unit->exec, unit->argv, unit->envp[0] ? unit->envp : NULL);
    service->pgid = service->pid;
    service->last_error = service->pid < 0 ? errno : 0;

    if (directory >= 0) {
        if (fchdir(directory) < 0)
            fprintf(stderr, "daemond: cannot restore working directory: errno=%d\n",
                    errno);

        close(directory);
    }

    if (service->pid >= 0 && setpgid(service->pid, service->pid) < 0) {
        /* A child which exited in this small race is still reaped normally. */
        if (errno != ESRCH) {
            int error = errno;

            service->last_error = error;

            kill(service->pid, SIGTERM);

            errno = error;

            return -1;
        }
    }

    service->start_on_exit = 0;

    return service->pid >= 0 ? 0 : -1;
}

static void report(const char* what, const struct service* service, int error) {
    const char* name = service->unit.name[0] ? service->unit.name : "?";

    if (error)
        fprintf(stderr, "daemond: %s: %s: errno=%d\n", what, name, error);
    else
        fprintf(stderr, "daemond: %s: %s\n", what, name);
}

static void reap_child(struct service* services, int count, int pid, int status) {
    for (int i = 0; i < count; ++i) {
        if (services[i].pid != pid)
            continue;

        fprintf(stderr, "daemond: reaped service pid=%d status=%d\n", pid, status);

        services[i].pid = services[i].pgid = -1;

        if (services[i].enabled &&
            (services[i].start_on_exit ||
             (services[i].unit.restart &&
              (!services[i].unit.failure_only || !WIFEXITED(status) ||
               WEXITSTATUS(status) != 0))) &&
            services[i].restarts < services[i].unit.max_restarts) {
            ++services[i].restarts;
            fprintf(stderr, "daemond: restarting service: %s attempt=%d\n",
                    services[i].unit.name, services[i].restarts);

            if (start_service(&services[i]) < 0) {
                services[i].last_error = errno;

                report("cannot restart service", &services[i], services[i].last_error);
            }
        }

        services[i].start_on_exit = 0;

        return;
    }
}

/* Stop every running service, escalating to SIGKILL when SIGTERM is ignored.
 * With restart set, services that were running are started again afterwards;
 * a stop failure leaves them stopped and is reported to the caller. */
static int stop_all_services(struct service* services, int count, int restart) {
    struct stop_process targets[SERVICE_MAX];

    int stop_error = 0;
    int stop_failed;

    for (int i = 0; i < count; ++i) {
        targets[i].pid       = services[i].pid;
        targets[i].pgid      = services[i].pgid;
        targets[i].status    = 0;
        targets[i].exited    = 0;
        targets[i].escalated = 0;

        services[i].enabled       = restart;
        services[i].start_on_exit = 0;
    }

    stop_failed =
        stop_processes(targets, count, SERVICE_STOP_GRACE_MS, SERVICE_STOP_KILL_MS) < 0;
    if (stop_failed)
        stop_error = errno;

    for (int i = 0; i < count; ++i) {
        if (services[i].pid < 0)
            continue;

        if (!targets[i].exited) {
            services[i].last_error = stop_error;

            report("cannot stop service", &services[i], services[i].last_error);
            continue;
        }

        fprintf(stderr, "daemond: reaped service pid=%d status=%d\n", services[i].pid,
                targets[i].status);

        if (targets[i].escalated)
            fprintf(stderr, "daemond: service pid=%d ignored SIGTERM; sent SIGKILL\n",
                    services[i].pid);

        services[i].pid = services[i].pgid = -1;
    }

    if (stop_failed) {
        errno = stop_error;

        return -1;
    }

    if (restart) {
        for (int i = 0; i < count; ++i) {
            if (services[i].pid >= 0 || !targets[i].exited)
                continue;

            if (start_service(&services[i]) < 0) {
                services[i].last_error = errno;

                report("cannot restart service", &services[i], services[i].last_error);

                return -1;
            }
        }
    }

    return 0;
}

/* A path is safe to bind only when it is absent or holds a socket that no
 * process is listening on.  Probe first so a live listener is never touched,
 * then remove only a stale socket; never remove a non-socket. */
static int claim_control_path(const char* path) {
    struct sockaddr_un address;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int flags = fcntl(fd, F_GETFL);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int saved = errno;

        close(fd);

        errno = saved;

        return -1;
    }

    memset(&address, 0, sizeof address);

    address.sun_family = AF_UNIX;

    if (strlen(path) >= sizeof address.sun_path) {
        close(fd);

        errno = ENAMETOOLONG;

        return -1;
    }

    strcpy(address.sun_path, path);

    int connected =
        connect(fd, (struct sockaddr*) &address, (unsigned) (2 + strlen(path)));
    int saved = errno;

    close(fd);

    if (connected == 0 || saved == EINPROGRESS || saved == EAGAIN) {
        errno = EADDRINUSE;

        return -1;
    }

    if (saved != ECONNREFUSED && saved != ENOENT) {
        errno = saved;

        return -1;
    }

    struct stat status;

    if (lstat(path, &status) < 0)
        return errno == ENOENT ? 0 : -1;

    if (!S_ISSOCK(status.st_mode)) {
        errno = ENOTSOCK;

        return -1;
    }

    if (saved != ECONNREFUSED) {
        errno = EADDRINUSE;

        return -1;
    }

    if (unlink(path) < 0 && errno != ENOENT)
        return -1;

    return 0;
}

static void remove_control_socket(void) {
    if (bound_control_path)
        unlink(bound_control_path);
}

static int open_control(void) {
    const char* paths[] = {DAEMON_SOCKET_PATH, DAEMON_SOCKET_FALLBACK};

    int error = 0;

    for (int i = 0; i < 2; ++i) {
        if (claim_control_path(paths[i]) < 0) {
            int saved = errno;
            if (saved == EADDRINUSE)
                fprintf(stderr, "daemond: control socket %s is already in use\n",
                        paths[i]);
            else if (saved == ENOTSOCK)
                fprintf(stderr, "daemond: refusing to replace non-socket %s\n", paths[i]);
            else
                error = saved;

            errno = saved;

            if (saved == EADDRINUSE || saved == ENOTSOCK)
                return -1;

            continue;
        }

        struct sockaddr_un address;

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            error = errno;

            continue;
        }

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            error = errno;

            close(fd);
            continue;
        }

        memset(&address, 0, sizeof address);

        address.sun_family = AF_UNIX;

        if (strlen(paths[i]) >= sizeof address.sun_path) {
            error = ENAMETOOLONG;

            close(fd);
            continue;
        }

        strcpy(address.sun_path, paths[i]);

        if (bind(fd, (struct sockaddr*) &address, (unsigned) (2 + strlen(paths[i]))) ==
                0 &&
            listen(fd, 4) == 0) {
            if (chmod(paths[i], 0600) == 0) {
                bound_control_path = paths[i];

                return fd;
            }

            error = errno;
        }
        else {
            error = errno;
        }

        close(fd);

        if (error == EADDRINUSE) {
            errno = error;

            return -1;
        }
    }

    if (error)
        errno = error;

    return -1;
}

static int write_all(int fd, const void* data, size_t length) {
    const char* bytes = (const char*) data;

    while (length) {
        long written = write(fd, bytes, (unsigned long) length);

        if (written < 0 && errno == EINTR)
            continue;

        if (written <= 0)
            return -1;

        bytes += written;
        length -= (size_t) written;
    }

    return 0;
}

static void append_text(char* buffer, size_t* used, const char* text) {
    size_t length, room;

    if (*used >= DAEMON_RESPONSE_MAX - 1)
        return;

    length = strlen(text);
    room   = DAEMON_RESPONSE_MAX - 1 - *used;

    if (length > room)
        length = room;

    memcpy(buffer + *used, text, length);

    *used += length;
    buffer[*used] = '\0';
}

static long long monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;

    return (long long) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

enum request_status {
    REQUEST_CLOSED,
    REQUEST_COMPLETE,
    REQUEST_MALFORMED,
    REQUEST_TIMED_OUT,
    REQUEST_FAILED
};

static enum request_status receive_request(int client, char* request, size_t capacity,
                                           size_t* used) {
    const long long deadline = monotonic_ms() + CONTROL_REQUEST_TIMEOUT_MS;

    size_t length = 0;

    if (deadline < 0)
        return REQUEST_FAILED;

    for (;;) {
        struct pollfd ready = {client, POLLIN, 0};

        long long now = monotonic_ms();

        int result;

        if (now < 0 || now >= deadline)
            return REQUEST_TIMED_OUT;

        result = poll(&ready, 1, (int) (deadline - now));
        if (result < 0) {
            if (errno == EINTR) {
                if (shutdown_pending)
                    return REQUEST_TIMED_OUT;

                continue;
            }

            return REQUEST_FAILED;
        }

        if (result == 0)
            return REQUEST_TIMED_OUT;

        if (ready.revents & POLLNVAL)
            return REQUEST_FAILED;

        if (!(ready.revents & (POLLIN | POLLHUP | POLLERR)))
            continue;

        {
            const size_t room = capacity - 1 - length;

            ssize_t received;

            if (!room)
                return REQUEST_MALFORMED;

            received = recv(client, request + length, room, 0);
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;

                return REQUEST_FAILED;
            }

            if (received == 0) {
                if (!length)
                    return REQUEST_CLOSED;

                *used = length;

                return REQUEST_COMPLETE;
            }

            for (ssize_t i = 0; i < received; ++i) {
                if (request[length + (size_t) i] == '\n' ||
                    request[length + (size_t) i] == '\0') {
                    *used = length + (size_t) i + 1;

                    return REQUEST_COMPLETE;
                }
            }

            length += (size_t) received;
        }
    }
}

/* Requests arrive on the control socket, which the supervisor polls itself.
 * Handling them in the main loop keeps one process per supervisor: the old
 * helper fork inherited the daemond name and showed up as a second daemond in
 * `ps`. */
static void handle_client(int client, struct service* services, int count) {
    char request[DAEMON_PROTOCOL_MAX];
    char reply[DAEMON_RESPONSE_MAX];

    struct command command;

    size_t length = 0;

    const enum request_status status =
        receive_request(client, request, sizeof request, &length);

    if (status == REQUEST_CLOSED)
        return;

    if (status == REQUEST_TIMED_OUT) {
        fprintf(stderr, "daemond: control request timed out\n");

        return;
    }

    if (status == REQUEST_FAILED) {
        fprintf(stderr, "daemond: control request failed: errno=%d\n", errno);

        return;
    }

    if (status == REQUEST_MALFORMED) {
        (void) write_all(client, "ERR malformed request\n",
                         sizeof "ERR malformed request\n" - 1);

        return;
    }

    request[length] = '\0';

    if (length && request[length - 1] == '\n')
        request[--length] = '\0';

    command.kind = -1;

    if (!strcmp(request, "STATUS"))
        command.kind = CMD_STATUS;
    else if (!strcmp(request, "START"))
        command.kind = CMD_START;
    else if (!strcmp(request, "STOP"))
        command.kind = CMD_STOP;
    else if (!strcmp(request, "RESTART"))
        command.kind = CMD_RESTART;
    else if (!strcmp(request, "CONFIG VALIDATE"))
        command.kind = CMD_VALIDATE;
    else if (!strcmp(request, "CONFIG LIST"))
        command.kind = CMD_LIST;

    reply[0] = '\0';

    if (command.kind == CMD_STATUS) {
        size_t used = 0;
        char   status_line[128];

        snprintf(status_line, sizeof status_line, "OK services=%d\n", count);
        append_text(reply, &used, status_line);

        for (int i = 0; i < count; ++i) {
            snprintf(status_line, sizeof status_line,
                     "service=%s state=%s restart=%d pid=%d pgid=%d error=%d\n",
                     services[i].unit.name,
                     services[i].pid >= 0 && services[i].enabled ? "running" : "stopped",
                     services[i].restarts, services[i].pid, services[i].pgid,
                     services[i].last_error);
            append_text(reply, &used, status_line);
        }
    }
    else if (command.kind == CMD_LIST) {
        size_t used = 0;

        append_text(reply, &used, "OK ");

        for (int i = 0; i < count; ++i) {
            append_text(reply, &used, services[i].unit.name);
            append_text(reply, &used, "\n");
        }
    }
    else if (command.kind == CMD_VALIDATE) {
        strcpy(reply, "OK valid\n");
    }
    else if (command.kind == CMD_STOP || command.kind == CMD_RESTART) {
        const int restart = command.kind == CMD_RESTART;

        /* With no configured services there is no child to wait for; STOP
         * is the explicit shutdown request for a resident idle supervisor. */
        if (command.kind == CMD_STOP && count == 0)
            shutdown_pending = 1;

        if (stop_all_services(services, count, restart) < 0) {
            fprintf(stderr, "daemond: cannot stop services: errno=%d\n", errno);

            strcpy(reply, "ERR stop failed\n");
        }
        else {
            strcpy(reply, restart ? "OK restarting\n" : "OK stopped\n");
        }
    }
    else if (command.kind == CMD_START) {
        int ok = 1;

        for (int i = 0; i < count; ++i) {
            services[i].enabled       = 1;
            services[i].start_on_exit = services[i].pid >= 0;

            if (services[i].pid < 0 && start_service(&services[i]) < 0) {
                report("cannot start service", &services[i], services[i].last_error);

                ok = 0;
            }
        }

        strcpy(reply, ok ? "OK started\n" : "ERR start failed\n");
    }
    else {
        strcpy(reply, "ERR malformed request\n");
    }

    if (write_all(client, reply, strlen(reply)) < 0)
        fprintf(stderr, "daemond: control reply failed: errno=%d\n", errno);
}

int astia_daemon_run(const char* unit_directory) {
    const char* directory = unit_directory ? unit_directory : "/etc/daemond";

    static struct unit    units[SERVICE_MAX];
    static struct service services[SERVICE_MAX];

    int count = 0, failures = 0, control = -1;

    memset(services, 0, sizeof services);

    if (units_load(directory, units, SERVICE_MAX, &count) < 0)
        return -1;

    for (int i = 0; i < count; ++i) {
        services[i].unit          = units[i];
        services[i].pid           = -1;
        services[i].pgid          = -1;
        services[i].enabled       = 1;
        services[i].start_on_exit = 0;
        services[i].last_error    = 0;
        services[i].restarts      = 0;

        unit_rebind(&services[i].unit);
    }

    if (!count)
        fprintf(stderr, "daemond: no services configured; idling\n");

    control = open_control();
    if (control < 0) {
        fprintf(stderr, "daemond: control socket failed: errno=%d (%s)\n", errno,
                strerror(errno));

        return -1;
    }

    signal(SIGTERM, terminate_signal);
    signal(SIGINT, terminate_signal);
    fprintf(stderr, "daemond: started\n");

    for (int i = 0; i < count; ++i) {
        if (start_service(&services[i]) < 0) {
            report("cannot start service", &services[i], services[i].last_error);
            ++failures;
        }
    }

    for (;;) {
        int status, pid;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            reap_child(services, count, pid, status);
        }

        int running = 0;

        for (int i = 0; i < count; ++i) {
            if (services[i].pid >= 0)
                ++running;
        }

        if (shutdown_pending) {
            if (stop_all_services(services, count, 0) < 0) {
                fprintf(stderr, "daemond: cannot stop all services: errno=%d\n", errno);

                ++failures;
            }

            break;
        }
        else if (count > 0 && running == 0) {
            /* Every configured service has exited and none was restarted.
             * A configuration with no services stays resident instead. */
            break;
        }

        struct pollfd watch;

        watch.fd      = control;
        watch.events  = POLLIN;
        watch.revents = 0;

        const int ready = poll(&watch, 1, 200);
        if (ready < 0) {
            if (errno == EINTR)
                continue;

            fprintf(stderr, "daemond: control poll failed: errno=%d\n", errno);

            shutdown_pending = 1;

            continue;
        }

        if (ready == 0)
            continue;

        if (watch.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "daemond: control socket error revents=0x%x\n",
                    watch.revents);

            shutdown_pending = 1;

            continue;
        }

        if (!(watch.revents & POLLIN))
            continue;

        const int client = accept(control, 0, 0);
        if (client < 0) {
            if (errno != EINTR)
                fprintf(stderr, "daemond: control accept failed: errno=%d\n", errno);

            continue;
        }

        /* A restart or start spawns services while this request is being
         * handled; the new child must not inherit the control connection. */
        if (fcntl(client, F_SETFD, FD_CLOEXEC) < 0) {
            fprintf(stderr, "daemond: control client setup failed: errno=%d\n", errno);

            close(client);

            continue;
        }

        handle_client(client, services, count);
        close(client);
    }

    close(control);
    remove_control_socket();

    if (failures)
        fprintf(stderr, "daemond: shutdown with failures\n");
    else
        fprintf(stderr, "daemond: clean shutdown\n");

    return failures ? -1 : 0;
}

int main(void) {
    return astia_daemon_run("/etc/daemond") == 0 ? 0 : 1;
}
