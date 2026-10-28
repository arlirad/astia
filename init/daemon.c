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
    int   argc, pid, pgid, restart, failure_only, restarts, max_restarts, enabled;
    int   last_error;
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
static volatile sig_atomic_t command_pending;
static void                  command_signal(int signal) {
    (void) signal;
    command_pending = 1;
}

static int decimal(const char* text, int* result) {
    unsigned v = 0;
    int      d = 0;
    while (*text >= '0' && *text <= '9') {
        v = v * 10u + (unsigned) (*text - '0');
        if (v > SERVICE_RESTART_MAX)
            return -1;
        ++text;
        d = 1;
    }
    if (!d || *text)
        return -1;
    *result = (int) v;
    return 0;
}
static int option_value(const char* token, const char* name, int* value) {
    size_t n = strlen(name);
    if (strncmp(token, name, n) || token[n] != '=')
        return 0;
    return decimal(token + n + 1, value) == 0 ? 1 : -1;
}
static int parse_line(struct service* s) {
    char* c = s->line;
    s->argc = s->restart = s->failure_only = s->restarts = s->max_restarts = 0;
    while (*c == ' ' || *c == '\t')
        ++c;
    if (!*c || *c == '#')
        return 0;
    while (*c) {
        char* t;
        while (*c == ' ' || *c == '\t')
            ++c;
        if (!*c || *c == '#')
            break;
        t = c;
        while (*c && *c != ' ' && *c != '\t')
            ++c;
        if (*c)
            *c++ = '\0';
        if (!strcmp(t, "respawn") || !strcmp(t, "restart")) {
            s->restart      = 1;
            s->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        if (!strcmp(t, "respawn-on-failure")) {
            s->restart      = 1;
            s->failure_only = 1;
            s->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        {
            int p = option_value(t, "respawn", &s->max_restarts);
            if (!p)
                p = option_value(t, "restart", &s->max_restarts);
            if (!p)
                p = option_value(t, "max-restarts", &s->max_restarts);
            if (p < 0)
                return -1;
            if (p) {
                s->restart = s->max_restarts != 0;
                continue;
            }
        }
        if (s->restart && s->argc && s->max_restarts == SERVICE_DEFAULT_RESTARTS) {
            int limit;
            if (!decimal(t, &limit)) {
                s->max_restarts = limit;
                continue;
            }
        }
        if (s->argc == SERVICE_ARG_MAX || (s->argc == 0 && t[0] != '/'))
            return -1;
        s->argv[s->argc++] = t;
    }
    if (!s->argc)
        return 0;
    s->argv[s->argc] = 0;
    s->pid           = -1;
    s->enabled       = 1;
    return 1;
}
static int start_service(struct service* s) {
    if (s->pid >= 0)
        return 0;
    s->pid = spawnve(s->argv[0], s->argv, 0);
    s->pgid = s->pid;
    s->last_error = s->pid < 0 ? errno : 0;
    if (s->pid >= 0 && setpgid(s->pid, s->pid) < 0) {
        s->pgid = -1;
        s->last_error = errno;
    }
    return s->pid >= 0 ? 0 : -1;
}
static void report(const char* what, const struct service* s) {
    fprintf(stderr, "daemon: %s: %s\n", what, s->argc ? s->argv[0] : "?");
}
static int open_control(void) {
    const char* paths[] = {DAEMON_SOCKET_PATH, DAEMON_SOCKET_FALLBACK};
    for (int i = 0; i < 2; ++i) {
        struct sockaddr_un a;
        int                fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
            continue;
        unlink(paths[i]);
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        strncpy(a.sun_path, paths[i], sizeof a.sun_path - 1);
        if (bind(fd, (struct sockaddr*) &a, (unsigned) (2 + strlen(paths[i]))) == 0 &&
            listen(fd, 4) == 0) {
            chmod(paths[i], 0600);
            return fd;
        }
        close(fd);
    }
    return -1;
}
static void response(int fd, const char* text) {
    (void) write(fd, text, (unsigned long) strlen(text));
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

/* The helper owns no service state. It only frames one request and hands it to
 * the supervisor; SIGUSR1 wakes the supervisor without adding a shell or a
 * second policy owner. */
static int control_helper(int listen_fd, int input, int output) {
    for (;;) {
        int            client = accept(listen_fd, 0, 0);
        char           request[DAEMON_PROTOCOL_MAX];
        struct command c;
        long           n;
        if (client < 0)
            continue;
        n = recv(client, request, sizeof request - 1, 0);
        if (n <= 0) {
            close(client);
            continue;
        }
        request[n] = '\0';
        if (n >= 2 && request[n - 1] == '\n')
            request[n - 1] = '\0';
        c.kind = -1;
        if (!strcmp(request, "STATUS"))
            c.kind = CMD_STATUS;
        else if (!strcmp(request, "START"))
            c.kind = CMD_START;
        else if (!strcmp(request, "STOP"))
            c.kind = CMD_STOP;
        else if (!strcmp(request, "RESTART"))
            c.kind = CMD_RESTART;
        else if (!strcmp(request, "CONFIG VALIDATE"))
            c.kind = CMD_VALIDATE;
        else if (!strcmp(request, "CONFIG LIST"))
            c.kind = CMD_LIST;
        write(input, &c, sizeof c);
        kill(getppid(), SIGUSR1);
        n = read(output, request, sizeof request - 1);
        if (n > 0)
            response(client, request);
        else
            response(client, "ERR daemon stopped\n");
        close(client);
    }
}

int astia_daemon_run(const char* rc_path) {
    FILE*          file = fopen(rc_path ? rc_path : "/etc/rc", "r");
    struct service services[SERVICE_MAX];
    char           line[SERVICE_LINE], reply[DAEMON_RESPONSE_MAX];
    int count = 0, failures = 0, control = -1, commands[2], replies[2], helper = -1;
    if (!file) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    fprintf(stderr, "daemon: started\n");
    while (fgets(line, sizeof line, file)) {
        size_t          n = strlen(line);
        struct service* s;
        int             parsed;
        if (n == sizeof line - 1 && line[n - 1] != '\n' && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {
            }
            ++failures;
            continue;
        }
        if (n && line[n - 1] == '\n')
            line[--n] = '\0';
        if (n && line[n - 1] == '\r')
            line[--n] = '\0';
        if (count == SERVICE_MAX) {
            ++failures;
            continue;
        }
        s = &services[count];
        memcpy(s->line, line, n + 1);
        parsed = parse_line(s);
        if (parsed < 0) {
            ++failures;
            report("malformed service record", s);
        }
        else if (parsed > 0)
            ++count;
    }
    fclose(file);
    if (failures)
        return -1;
    if (!count)
        return fprintf(stderr, "daemon: clean shutdown\n"), 0;
    control = open_control();
    if (control < 0)
        return -1;
    pipe(commands);
    pipe(replies);
    signal(SIGUSR1, command_signal);
    helper = fork();
    if (helper == 0) {
        close(commands[0]);
        close(replies[1]);
        return control_helper(control, commands[1], replies[0]);
    }
    close(commands[1]);
    close(replies[0]);
    int active = 0;
    for (int i = 0; i < count; ++i)
        if (start_service(&services[i]) < 0) {
            report("cannot start service", &services[i]);
            ++failures;
        }
        else
            ++active;
    while (active > 0 || helper > 0) {
        int            status, pid;
        struct command c;
        if (command_pending) {
            command_pending = 0;
            if (read(commands[0], &c, sizeof c) == sizeof c) {
                int ok   = 1;
                reply[0] = '\0';
                if (c.kind == CMD_STATUS) {
                    size_t used = 0;
                    char   line[128];
                    snprintf(line, sizeof line, "OK services=%d\n", count);
                    append_text(reply, &used, line);
                    for (int i = 0; i < count; ++i) {
                        snprintf(line, sizeof line,
                                 "service=%s state=%s restart=%d pid=%d pgid=%d error=%d\n",
                                 services[i].argv[0],
                                 services[i].pid >= 0 && services[i].enabled ? "running" : "stopped",
                                 services[i].restarts, services[i].pid, services[i].pgid,
                                 services[i].last_error);
                        append_text(reply, &used, line);
                    }
                }
                else if (c.kind == CMD_LIST) {
                    size_t used = 0;
                    append_text(reply, &used, "OK ");
                    for (int i = 0; i < count; ++i) {
                        append_text(reply, &used, services[i].argv[0]);
                        append_text(reply, &used, "\n");
                    }
                }
                else if (c.kind == CMD_VALIDATE)
                    strcpy(reply, "OK valid\n");
                else if (c.kind == CMD_STOP || c.kind == CMD_RESTART) {
                    for (int i = 0; i < count; ++i) {
                        services[i].enabled = c.kind == CMD_RESTART;
                        if (services[i].pid >= 0)
                            kill(-services[i].pid, SIGTERM);
                    }
                    strcpy(reply,
                           c.kind == CMD_STOP ? "OK stopped\n" : "OK restarting\n");
                }
                else if (c.kind == CMD_START) {
                    for (int i = 0; i < count; ++i) {
                        services[i].enabled = 1;
                        if (start_service(&services[i]) < 0)
                            ok = 0;
                    }
                    strcpy(reply, ok ? "OK started\n" : "ERR start failed\n");
                }
                else
                    strcpy(reply, "ERR malformed request\n");
                write(replies[1], reply, (unsigned long) strlen(reply));
            }
        }
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < count; ++i)
                if (services[i].pid == pid) {
                    services[i].pid = -1;
                    services[i].pgid = -1;
                    --active;
                    if (services[i].enabled && services[i].restart &&
                        services[i].restarts < services[i].max_restarts) {
                        ++services[i].restarts;
                        if (start_service(&services[i]) == 0)
                            ++active;
                    }
                    break;
                }
        }
        if (active == 0)
            break;
    }
    if (helper > 0)
        kill(helper, SIGTERM);
    close(control);
    unlink(DAEMON_SOCKET_PATH);
    unlink(DAEMON_SOCKET_FALLBACK);
    fprintf(stderr, "daemon: clean shutdown\n");
    return failures ? -1 : 0;
}
int main(void) {
    return astia_daemon_run("/etc/rc") == 0 ? 0 : 1;
}
