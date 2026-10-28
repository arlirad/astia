#include "daemon.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* These are deliberately compile-time limits.  The supervisor never needs a
 * heap or a shell to turn configuration into processes. */
enum {
    SERVICE_LINE = 256,
    SERVICE_MAX = 32,
    SERVICE_ARG_MAX = 16,
    SERVICE_RESTART_MAX = 16,
    SERVICE_DEFAULT_RESTARTS = 3
};

struct service {
    char line[SERVICE_LINE];
    char* argv[SERVICE_ARG_MAX + 1];
    int argc;
    int pid;
    int restart;
    int failure_only;
    int restarts;
    int max_restarts;
};

static int decimal(const char* text, int* result) {
    unsigned value = 0;
    int digits = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (unsigned)(*text - '0');
        if (value > SERVICE_RESTART_MAX) return -1;
        ++text;
        digits = 1;
    }
    if (!digits || *text != '\0') return -1;
    *result = (int)value;
    return 0;
}

static int option_value(const char* token, const char* name, int* value) {
    const size_t length = strlen(name);
    if (strncmp(token, name, length) != 0 || token[length] != '=') return 0;
    return decimal(token + length + 1, value) == 0 ? 1 : -1;
}

/* Parse one record in-place.  There is intentionally no quoting, expansion,
 * redirection, pipeline, or command substitution syntax. */
static int parse_line(struct service* service) {
    char* cursor = service->line;
    service->argc = 0;
    service->restart = 0;
    service->failure_only = 0;
    service->restarts = 0;
    service->max_restarts = 0;

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == '\0' || *cursor == '#') return 0;
    while (*cursor != '\0') {
        char* token;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0' || *cursor == '#') break;
        token = cursor;
        while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';

        if (strcmp(token, "respawn") == 0 || strcmp(token, "restart") == 0) {
            service->restart = 1;
            service->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        if (strcmp(token, "respawn-on-failure") == 0) {
            service->restart = 1;
            service->failure_only = 1;
            service->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        {
            int parsed = option_value(token, "respawn", &service->max_restarts);
            if (parsed == 0) parsed = option_value(token, "restart", &service->max_restarts);
            if (parsed == 0) parsed = option_value(token, "max-restarts", &service->max_restarts);
            if (parsed < 0) return -1;
            if (parsed != 0) {
                service->restart = service->max_restarts != 0;
                continue;
            }
        }
        if (service->restart && service->argc > 0 &&
            service->max_restarts == SERVICE_DEFAULT_RESTARTS) {
            int limit;
            if (decimal(token, &limit) == 0) {
                service->max_restarts = limit;
                continue;
            }
        }
        if (service->argc == SERVICE_ARG_MAX ||
            (service->argc == 0 && token[0] != '/')) return -1;
        service->argv[service->argc++] = token;
    }
    if (service->argc == 0) return 0;
    service->argv[service->argc] = NULL;
    service->pid = -1;
    return 1;
}

static int start(struct service* service) {
    /* spawnve creates the service's session and process group.  Keeping that
     * kernel contract here means group signalling and ctty inheritance stay
     * identical for every service and for zsh. */
    service->pid = spawnve(service->argv[0], service->argv, NULL);
    return service->pid >= 0 ? 0 : -1;
}

static void report(const char* what, const struct service* service) {
    fprintf(stderr, "daemon: %s: %s\n", what, service->argc ? service->argv[0] : "?");
}

int astia_daemon_run(const char* rc_path) {
    FILE* file = fopen(rc_path ? rc_path : "/etc/rc", "r");
    struct service services[SERVICE_MAX];
    char line[SERVICE_LINE];
    int count = 0;
    int active = 0;
    int failures = 0;

    fprintf(stderr, "daemon: started\n");

    if (!file) {
        if (errno == ENOENT) return 0; /* an empty service set is valid */
        fprintf(stderr, "daemon: cannot open %s\n", rc_path ? rc_path : "/etc/rc");
        return -1;
    }
    while (fgets(line, sizeof line, file) != NULL) {
        struct service* service;
        size_t length = strlen(line);
        int parsed;
        if (length == sizeof line - 1 && line[length - 1] != '\n' && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF) {}
            ++failures;
            continue;
        }
        if (length && line[length - 1] == '\n') line[--length] = '\0';
        if (length && line[length - 1] == '\r') line[--length] = '\0';
        if (count == SERVICE_MAX) {
            ++failures;
            continue;
        }
        service = &services[count];
        memcpy(service->line, line, length + 1);
        parsed = parse_line(service);
        if (parsed < 0) {
            ++failures;
            report("malformed service record", service);
        } else if (parsed > 0) {
            ++count;
        }
    }
    fclose(file);
    if (failures) return -1; /* malformed configuration never partially boots */

    for (int i = 0; i < count; ++i) {
        struct service* service = &services[i];
        if (start(service) == 0) {
            ++active;
            continue;
        }
        report("cannot start service", service);
        while (service->restart && service->restarts < service->max_restarts) {
            ++service->restarts;
            if (start(service) == 0) {
                ++active;
                break;
            }
        }
        if (service->pid < 0) ++failures;
    }

    while (active > 0) {
        int status = 0;
        int pid = waitpid(-1, &status, 0);
        int index = -1;
        if (pid < 0) {
            fprintf(stderr, "daemon: waitpid failed\n");
            return -1;
        }
        for (int i = 0; i < count; ++i)
            if (services[i].pid == pid) { index = i; break; }
        if (index < 0) {
            fprintf(stderr, "daemon: reaped unknown child %d\n", pid);
            return -1;
        }
        struct service* service = &services[index];
        service->pid = -1;
        --active;
        int failed = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
        if (!service->restart || (service->failure_only && !failed)) {
            if (failed) { report("service failed", service); ++failures; }
            continue;
        }
        if (service->restarts >= service->max_restarts) {
            report("restart limit reached", service);
            ++failures;
            continue;
        }
        ++service->restarts;
        if (start(service) != 0) {
            report("cannot restart service", service);
            ++failures;
        } else {
            ++active;
        }
    }
    if (failures == 0) fprintf(stderr, "daemon: clean shutdown\n");
    return failures == 0 ? 0 : -1;
}

int main(void) { return astia_daemon_run("/etc/rc") == 0 ? 0 : 1; }
