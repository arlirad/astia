#include "daemon.h"

#include <daemon_protocol.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum {
    SERVICE_LINE             = 256,
    SERVICE_MAX              = 32,
    SERVICE_ARG_MAX          = 16,
    SERVICE_RESTART_MAX      = 16,
    SERVICE_DEFAULT_RESTARTS = 3
};

struct service {
    char  line[SERVICE_LINE];
    char* argv[SERVICE_ARG_MAX + 1];
    int   argc, pid, pgid, restart, failure_only, restarts, max_restarts;
    int   enabled, start_on_exit, last_error;
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

static volatile sig_atomic_t work_pending;
static volatile sig_atomic_t command_pending;
static volatile sig_atomic_t shutdown_pending;

static void work_signal(int signal_number) {
    if (signal_number == SIGTERM || signal_number == SIGINT)
        shutdown_pending = 1;
    if (signal_number == SIGUSR1)
        command_pending = 1;
    work_pending = 1;
}

static int decimal(const char* text, int* result) {
    unsigned value = 0;
    int      digit = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (unsigned) (*text - '0');
        if (value > SERVICE_RESTART_MAX)
            return -1;
        ++text;
        digit = 1;
    }
    if (!digit || *text)
        return -1;
    *result = (int) value;
    return 0;
}

static int option_value(const char* token, const char* name, int* value) {
    size_t length = strlen(name);
    if (strncmp(token, name, length) || token[length] != '=')
        return 0;
    return decimal(token + length + 1, value) == 0 ? 1 : -1;
}

/* Parse only the native record format.  In particular, policy words are not
 * passed to a shell and may only occur at the end of a record. */
static int parse_line(struct service* service) {
    char* cursor = service->line;
    char* tokens[SERVICE_ARG_MAX + 3];
    int   token_count = 0;
    int   policy = 0;

    service->argc = service->restart = service->failure_only = 0;
    service->restarts = service->max_restarts = service->start_on_exit = 0;
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    if (!*cursor || *cursor == '#')
        return 0;
    while (*cursor) {
        char* token;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (!*cursor || *cursor == '#')
            break;
        token = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t')
            ++cursor;
        if (*cursor)
            *cursor++ = '\0';
        if (token_count == (int) (sizeof tokens / sizeof tokens[0]))
            return -1;
        tokens[token_count++] = token;
    }
    if (!token_count || tokens[0][0] != '/')
        return -1;

    /* A policy is a suffix, never an argument followed by another policy. */
    if (!strcmp(tokens[token_count - 1], "respawn") ||
        !strcmp(tokens[token_count - 1], "restart")) {
        policy = 1;
        service->restart = 1;
        service->max_restarts = SERVICE_DEFAULT_RESTARTS;
        --token_count;
    }
    else if (!strcmp(tokens[token_count - 1], "respawn-on-failure")) {
        policy = 1;
        service->restart = service->failure_only = 1;
        service->max_restarts = SERVICE_DEFAULT_RESTARTS;
        --token_count;
    }
    else {
        int limit = 0;
        int found = 0;
        const char* names[] = {"respawn", "restart", "max-restarts"};
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
            int parsed = option_value(tokens[token_count - 1], names[i], &limit);
            if (parsed < 0)
                return -1;
            if (parsed) {
                found = 1;
                break;
            }
        }
        if (found) {
            policy = 1;
            service->max_restarts = limit;
            service->restart = limit != 0;
            --token_count;
        }
    }
    if (policy && token_count == 0)
        return -1;
    if (token_count > SERVICE_ARG_MAX)
        return -1;
    for (int i = 0; i < token_count; ++i)
        service->argv[service->argc++] = tokens[i];
    service->argv[service->argc] = 0;
    service->pid = service->pgid = -1;
    service->enabled = 1;
    return 1;
}

static int start_service(struct service* service) {
    if (service->pid >= 0)
        return 0;
    service->pid = spawnve(service->argv[0], service->argv, 0);
    service->pgid = service->pid;
    service->last_error = service->pid < 0 ? errno : 0;
    if (service->pid >= 0 && setpgid(service->pid, service->pid) < 0) {
        /* A child which exited in this small race is still reaped normally. */
        if (errno != ESRCH) {
            service->last_error = errno;
            kill(service->pid, SIGTERM);
            return -1;
        }
    }
    service->start_on_exit = 0;
    return service->pid >= 0 ? 0 : -1;
}

static void stop_service(struct service* service) {
    if (service->pid < 0)
        return;
    if (service->pgid >= 0) {
        if (kill(-service->pgid, SIGTERM) < 0 && errno != ESRCH)
            service->last_error = errno;
    }
    else if (kill(service->pid, SIGTERM) < 0 && errno != ESRCH)
        service->last_error = errno;
}

static void reap_child(struct service* services, int count, int pid, int status,
                       int* helper) {
    if (pid == *helper) {
        *helper = -1;
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (services[i].pid != pid)
            continue;
        services[i].pid = services[i].pgid = -1;
        if (services[i].enabled &&
            (services[i].start_on_exit ||
             (services[i].restart &&
              (!services[i].failure_only || !WIFEXITED(status) || WEXITSTATUS(status) != 0))) &&
            services[i].restarts < services[i].max_restarts) {
            ++services[i].restarts;
            if (start_service(&services[i]) < 0)
                services[i].last_error = errno;
        }
        services[i].start_on_exit = 0;
        return;
    }
}

static void report(const char* what, const struct service* service) {
    fprintf(stderr, "daemon: %s: %s\n", what,
            service->argc ? service->argv[0] : "?");
}

static int open_control(void) {
    const char* paths[] = {DAEMON_SOCKET_PATH, DAEMON_SOCKET_FALLBACK};
    for (int i = 0; i < 2; ++i) {
        struct sockaddr_un address;
        int                fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            continue;
        unlink(paths[i]);
        memset(&address, 0, sizeof address);
        address.sun_family = AF_UNIX;
        if (strlen(paths[i]) >= sizeof address.sun_path) {
            close(fd);
            continue;
        }
        strcpy(address.sun_path, paths[i]);
        if (bind(fd, (struct sockaddr*) &address,
                 (unsigned) (2 + strlen(paths[i]))) == 0 && listen(fd, 4) == 0) {
            if (chmod(paths[i], 0600) == 0)
                return fd;
        }
        close(fd);
    }
    return -1;
}

static int write_all(int fd, const void* data, size_t length) {
    const char* bytes = (const char*) data;
    while (length) {
        long written = write(fd, bytes, (unsigned long) length);
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
    room = DAEMON_RESPONSE_MAX - 1 - *used;
    if (length > room)
        length = room;
    memcpy(buffer + *used, text, length);
    *used += length;
    buffer[*used] = '\0';
}

/* The helper owns no service state.  It serializes one complete request and
 * lets the supervisor perform all policy and process-group operations. */
static int control_helper(int listen_fd, int input, int output) {
    for (;;) {
        int            client = accept(listen_fd, 0, 0);
        char           request[DAEMON_PROTOCOL_MAX];
        struct command command;
        long           length;
        if (client < 0) {
            if (errno == EINTR)
                continue;
            return 1;
        }
        length = recv(client, request, sizeof request - 1, 0);
        if (length <= 0 || length >= (long) sizeof request ||
            (request[length - 1] != '\n' && request[length - 1] != '\0')) {
            write_all(client, "ERR malformed request\n", 23);
            close(client);
            continue;
        }
        request[length] = '\0';
        if (length && request[length - 1] == '\n')
            request[--length] = '\0';
        command.kind = -1;
        if (!strcmp(request, "STATUS")) command.kind = CMD_STATUS;
        else if (!strcmp(request, "START")) command.kind = CMD_START;
        else if (!strcmp(request, "STOP")) command.kind = CMD_STOP;
        else if (!strcmp(request, "RESTART")) command.kind = CMD_RESTART;
        else if (!strcmp(request, "CONFIG VALIDATE")) command.kind = CMD_VALIDATE;
        else if (!strcmp(request, "CONFIG LIST")) command.kind = CMD_LIST;
        if (write_all(input, &command, sizeof command) < 0 ||
            kill(getppid(), SIGUSR1) < 0) {
            close(client);
            return 1;
        }
        length = read(output, request, sizeof request - 1);
        if (length > 0)
            write_all(client, request, (size_t) length);
        else
            write_all(client, "ERR daemon stopped\n", 19);
        close(client);
    }
}

int astia_daemon_run(const char* rc_path) {
    FILE*          file = fopen(rc_path ? rc_path : "/etc/rc", "r");
    struct service services[SERVICE_MAX];
    char           line[SERVICE_LINE], reply[DAEMON_RESPONSE_MAX];
    int count = 0, failures = 0, control = -1, commands[2] = {-1, -1};
    int replies[2] = {-1, -1}, helper = -1;
    if (!file)
        return errno == ENOENT ? 0 : -1;

    while (fgets(line, sizeof line, file)) {
        size_t n = strlen(line);
        int parsed;
        if (n == sizeof line - 1 && line[n - 1] != '\n' && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {}
            ++failures;
            continue;
        }
        if (n && line[n - 1] == '\n') line[--n] = '\0';
        if (n && line[n - 1] == '\r') line[--n] = '\0';
        if (count == SERVICE_MAX) {
            ++failures;
            continue;
        }
        memcpy(services[count].line, line, n + 1);
        parsed = parse_line(&services[count]);
        if (parsed < 0) {
            ++failures;
            report("malformed service record", &services[count]);
        }
        else if (parsed > 0)
            ++count;
    }
    fclose(file);
    if (failures || !count) {
        if (!failures)
            fprintf(stderr, "daemon: clean shutdown\n");
        return failures ? -1 : 0;
    }

    control = open_control();
    if (control < 0)
        return -1;
    if (pipe(commands) < 0 || pipe(replies) < 0) {
        close(control);
        if (commands[0] >= 0) { close(commands[0]); close(commands[1]); }
        return -1;
    }
    signal(SIGUSR1, work_signal);
    signal(SIGCHLD, work_signal);
    signal(SIGTERM, work_signal);
    signal(SIGINT, work_signal);
    helper = fork();
    if (helper == 0) {
        close(commands[0]); close(replies[1]);
        return control_helper(control, commands[1], replies[0]);
    }
    if (helper < 0) {
        close(control); close(commands[0]); close(commands[1]);
        close(replies[0]); close(replies[1]);
        return -1;
    }
    close(commands[1]); close(replies[0]);
    fprintf(stderr, "daemon: started\n");

    for (int i = 0; i < count; ++i)
        if (start_service(&services[i]) < 0) {
            report("cannot start service", &services[i]);
            ++failures;
        }

    {
        int waited_pid = 0;
        int have_command = 0;
        struct command pending_command;
    for (;;) {
        int status, pid;
        int running = 0;
        for (int i = 0; i < count; ++i)
            if (services[i].pid >= 0) ++running;
        if (shutdown_pending) {
            for (int i = 0; i < count; ++i) {
                services[i].enabled = 0;
                services[i].start_on_exit = 0;
                stop_service(&services[i]);
            }
        }
        if (have_command || command_pending) {
            struct command command;
            if (have_command) {
                command = pending_command;
                have_command = 0;
            }
            else if (read(commands[0], &command, sizeof command) != sizeof command) {
                shutdown_pending = 1;
                continue;
            }
            command_pending = 0;
            {
                int ok = 1;
                reply[0] = '\0';
                if (command.kind == CMD_STATUS) {
                    size_t used = 0;
                    char   status_line[128];
                    snprintf(status_line, sizeof status_line, "OK services=%d\n", count);
                    append_text(reply, &used, status_line);
                    for (int i = 0; i < count; ++i) {
                        snprintf(status_line, sizeof status_line,
                                 "service=%s state=%s restart=%d pid=%d pgid=%d error=%d\n",
                                 services[i].argv[0],
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
                        append_text(reply, &used, services[i].argv[0]);
                        append_text(reply, &used, "\n");
                    }
                }
                else if (command.kind == CMD_VALIDATE) strcpy(reply, "OK valid\n");
                else if (command.kind == CMD_STOP || command.kind == CMD_RESTART) {
                    for (int i = 0; i < count; ++i) {
                        services[i].enabled = command.kind == CMD_RESTART;
                        services[i].start_on_exit = command.kind == CMD_RESTART;
                        stop_service(&services[i]);
                    }
                    strcpy(reply, command.kind == CMD_STOP ? "OK stopped\n" : "OK restarting\n");
                }
                else if (command.kind == CMD_START) {
                    for (int i = 0; i < count; ++i) {
                        services[i].enabled = 1;
                        services[i].start_on_exit = services[i].pid >= 0;
                        if (services[i].pid < 0 && start_service(&services[i]) < 0)
                            ok = 0;
                    }
                    strcpy(reply, ok ? "OK started\n" : "ERR start failed\n");
                }
                else strcpy(reply, "ERR malformed request\n");
                if (write_all(replies[1], reply, strlen(reply)) < 0)
                    shutdown_pending = 1;
            }
        }
        if (waited_pid > 0) {
            reap_child(services, count, waited_pid, status, &helper);
            waited_pid = 0;
        }
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
            reap_child(services, count, pid, status, &helper);
        running = 0;
        for (int i = 0; i < count; ++i)
            if (services[i].pid >= 0) ++running;
        if (shutdown_pending && running == 0)
            break;
        if (!shutdown_pending && running == 0) {
            int desired = 0;
            for (int i = 0; i < count; ++i) desired |= services[i].enabled;
            if (!desired)
                continue;
            break;
        }
        work_pending = 0;
        if (!work_pending) {
            if (running == 0 && !shutdown_pending) {
                if (read(commands[0], &pending_command, sizeof pending_command) ==
                    sizeof pending_command)
                    have_command = 1;
                else
                    shutdown_pending = 1;
            }
            else if (running > 0) {
                waited_pid = waitpid(-1, &status, 0);
                if (waited_pid < 0 && errno != EINTR)
                    shutdown_pending = 1;
            }
        }
    }
    }
    if (helper > 0) {
        kill(helper, SIGTERM);
        waitpid(helper, 0, 0);
    }
    close(commands[0]); close(replies[1]); close(control);
    unlink(DAEMON_SOCKET_PATH); unlink(DAEMON_SOCKET_FALLBACK);
    fprintf(stderr, "daemon: clean shutdown\n");
    return failures ? -1 : 0;
}

int main(void) {
    return astia_daemon_run("/etc/rc") == 0 ? 0 : 1;
}
