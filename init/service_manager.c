#include "service_manager.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The limits are deliberately part of the parser rather than being supplied
 * by malloc or by a shell.  A service owns its input buffer for as long as it
 * can be restarted, so argv pointers remain valid between waits. */
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
    int respawn;
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

static int parse_line(struct service* service) {
    char* cursor = service->line;
    int next_restart_count = 0;
    service->argc = 0;
    service->respawn = 0;
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
            service->respawn = 1;
            service->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        if (strcmp(token, "respawn-on-failure") == 0) {
            service->respawn = 1;
            service->max_restarts = SERVICE_DEFAULT_RESTARTS;
            continue;
        }
        {
            int parsed = option_value(token, "respawn", &next_restart_count);
            if (parsed == 0) parsed = option_value(token, "restart", &next_restart_count);
            if (parsed == 0) parsed = option_value(token, "max-restarts", &next_restart_count);
            if (parsed < 0) return -1;
            if (parsed != 0) {
                service->respawn = next_restart_count != 0;
                service->max_restarts = next_restart_count;
                continue;
            }
        }
        /* A number immediately after bare respawn is its restart limit. */
        if (service->respawn && service->argc > 0 && service->max_restarts == SERVICE_DEFAULT_RESTARTS) {
            int parsed;
            if (decimal(token, &parsed) == 0) {
                service->max_restarts = parsed;
                continue;
            }
        }
        if (service->argc == SERVICE_ARG_MAX) return -1;
        if (service->argc == 0 && token[0] != '/') return -1;
        service->argv[service->argc++] = token;
    }

    if (service->argc == 0) return 0;
    service->argv[service->argc] = NULL;
    return 1;
}

static int start(struct service* service) {
    /* spawnve creates a fresh process/session group for the service.  Do not
     * call setpgid here: the child is already a session leader. */
    service->pid = spawnve(service->argv[0], service->argv, NULL);
    return service->pid >= 0 ? 0 : -1;
}

int astia_service_manager(const char* rc_path) {
    FILE* file = fopen(rc_path == NULL ? "/etc/rc" : rc_path, "r");
    struct service services[SERVICE_MAX];
    char discard[SERVICE_LINE];
    int count = 0;
    int active = 0;
    int failures = 0;

    if (file == NULL) return errno == ENOENT ? 0 : -1;
    while (count < SERVICE_MAX && fgets(services[count].line, SERVICE_LINE, file) != NULL) {
        struct service* service = &services[count];
        const size_t length = strlen(service->line);
        int parsed;
        if (length == SERVICE_LINE - 1 && service->line[length - 1] != '\n' && !feof(file)) {
            do { if (fgets(discard, sizeof discard, file) == NULL) break; }
            while (strchr(discard, '\n') == NULL);
            ++failures;
            continue;
        }
        if (length != 0 && service->line[length - 1] == '\n') service->line[length - 1] = '\0';
        {
            size_t end = strlen(service->line);
            while (end != 0 && service->line[end - 1] == '\r') service->line[--end] = '\0';
        }
        parsed = parse_line(service);
        if (parsed < 0) { ++failures; continue; }
        if (parsed == 0) continue;
        if (start(service) != 0) {
            while (service->respawn && service->restarts < service->max_restarts) {
                ++service->restarts;
                if (start(service) == 0) break;
            }
            if (service->pid < 0) {
                ++failures;
                continue;
            }
        }
        ++active;
        ++count;
    }
    /* More than SERVICE_MAX records is malformed, not silently ignored. */
    if (count == SERVICE_MAX && fgets(discard, sizeof discard, file) != NULL) ++failures;
    fclose(file);

    while (active != 0) {
        int status;
        const int pid = waitpid(-1, &status, 0);
        int i;
        if (pid < 0) return -1;
        for (i = 0; i < count; ++i) if (services[i].pid == pid) break;
        if (i == count) return -1;
        services[i].pid = -1;
        --active;
        if (status == 0) {
            if (!services[i].respawn) continue;
        } else if (!services[i].respawn) {
            ++failures;
            continue;
        }
        if (services[i].restarts >= services[i].max_restarts) {
            if (status != 0 || services[i].respawn) ++failures;
            continue;
        }
        ++services[i].restarts;
        if (start(&services[i]) != 0) {
            ++failures;
            continue;
        }
        ++active;
    }
    return failures == 0 ? 0 : -1;
}
