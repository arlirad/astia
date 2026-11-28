#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include <sys/wait.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LIFECYCLE_MODULE_PATH "/lib/sysutils-lifecycle.km"
#define LIFECYCLE_MODULE_NAME "sysutils-lifecycle.km"
#define UNPRIVILEGED_UID 1u

static char* const environment[] = {(char*) "PATH=/bin", NULL};

static int wait_child(int pid) {
    int status = 0;

    if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}

static int run(const char* path, char* const argv[]) {
    const int pid = spawnve(path, argv, environment);
    if (pid < 0) {
        printf("runner: spawn %s failed\n", path);

        return -1;
    }

    return wait_child(pid);
}

static int run_as(const char* path, char* const argv[], unsigned uid) {
    const int pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        if (seteuid(uid) != 0)
            _exit(126);

        execve(path, argv, environment);
        _exit(127);
    }

    return wait_child(pid);
}

static int run_capture(const char* path, char* const argv[], char* output,
                       size_t capacity) {
    int fds[2];

    if (capacity == 0 || pipe(fds) != 0)
        return -1;

    const int pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);

        return -1;
    }

    if (pid == 0) {
        close(fds[0]);

        if (dup2(fds[1], STDOUT_FILENO) < 0)
            _exit(126);

        close(fds[1]);

        execve(path, argv, environment);
        _exit(127);
    }

    close(fds[1]);

    size_t used = 0;

    while (used + 1 < capacity) {
        const long got = read(fds[0], output + used, capacity - 1 - used);
        if (got <= 0)
            break;

        used += (size_t) got;
    }

    while (used + 1 >= capacity) {
        char scratch[32];

        if (read(fds[0], scratch, sizeof scratch) <= 0)
            break;
    }

    output[used] = '\0';

    close(fds[0]);

    return wait_child(pid);
}

static int parse_token(const char* text, astia_module_id* out) {
    char* end = NULL;

    const unsigned long slot = strtoul(text, &end, 10);

    if (end == text || *end != ':' || slot >= ASTIA_MODULE_SLOT_NONE)
        return 0;

    const char* generation_text = end + 1;

    const unsigned long long generation = strtoull(generation_text, &end, 10);

    if (end == generation_text || (*end != '\0' && *end != '\n' && *end != '\r'))
        return 0;

    out->slot       = (uint32_t) slot;
    out->reserved   = 0;
    out->generation = generation;

    return 1;
}

static const astia_module_record* find_record(const astia_module_record* records,
                                              uint32_t count, const astia_module_id* id) {
    for (uint32_t i = 0; i < count; ++i) {
        if (records[i].id.slot == id->slot && records[i].id.generation == id->generation)
            return &records[i];
    }

    return NULL;
}

int main(void) {
    astia_module_record    records[ASTIA_MODULE_LIST_MAX] = {};
    astia_module_list_info info                           = {};
    astia_module_stat      stat                           = {};
    astia_module_id        opened                         = {};

    char token[64];

    char* ls[]                  = {(char*) "modls", NULL};
    char* stat_argv[]           = {(char*) "modstat", token, NULL};
    char* open_argv[]           = {(char*) "modopen", (char*) "/missing-module.o", NULL};
    char* close_argv[]          = {(char*) "modclose", token, NULL};
    char* lifecycle_open_argv[] = {(char*) "modopen", (char*) LIFECYCLE_MODULE_PATH,
                                   NULL};

    if (run("/bin/modls", ls) != 0) {
        printf("runner: modls failed\n");

        return 10;
    }

    if (astia_modls(records, ASTIA_MODULE_LIST_MAX, &info) < 0 || info.count == 0) {
        printf("runner: no published module to stat\n");

        return 11;
    }

    const uint32_t baseline = info.total;

    snprintf(token, sizeof token, "%u:%llu", (unsigned) records[0].id.slot,
             (unsigned long long) records[0].id.generation);

    printf("runner: first module token %s\n", token);

    if (run("/bin/modstat", stat_argv) != 0) {
        printf("runner: modstat token failed\n");

        return 12;
    }

    if (run("/bin/modopen", open_argv) == 0) {
        printf("runner: modopen unexpectedly succeeded\n");

        return 13;
    }

    if (run("/bin/modclose", close_argv) == 0) {
        printf("runner: modclose of a resident module unexpectedly succeeded\n");

        return 14;
    }

    if (run_capture("/bin/modopen", lifecycle_open_argv, token, sizeof token) != 0) {
        printf("runner: lifecycle modopen failed\n");

        return 15;
    }

    token[strcspn(token, "\r\n")] = '\0';

    if (!parse_token(token, &opened)) {
        printf("runner: lifecycle token malformed\n");

        return 16;
    }

    printf("runner: lifecycle module token %s\n", token);

    if (astia_modls(records, ASTIA_MODULE_LIST_MAX, &info) < 0 ||
        info.total != baseline + 1) {
        printf("runner: lifecycle module not published\n");

        return 17;
    }

    const astia_module_record* loaded = find_record(records, info.count, &opened);

    if (loaded == NULL || strcmp(loaded->name, LIFECYCLE_MODULE_NAME) != 0 ||
        loaded->image_size == 0 || loaded->active_refs != 0 ||
        loaded->state != ASTIA_MODULE_STATE_LOADED ||
        (loaded->flags & ASTIA_MODULE_FLAG_UNLOADABLE) == 0) {
        printf("runner: lifecycle module snapshot invalid\n");

        return 17;
    }

    if (run("/bin/modstat", stat_argv) != 0) {
        printf("runner: lifecycle modstat failed\n");

        return 18;
    }

    if (run("/bin/modclose", close_argv) != 0) {
        printf("runner: lifecycle modclose failed\n");

        return 19;
    }

    if (astia_modstat(&opened, &stat) != -ENOENT) {
        printf("runner: closed module still visible\n");

        return 20;
    }

    if (astia_modls(records, ASTIA_MODULE_LIST_MAX, &info) < 0 ||
        info.total != baseline) {
        printf("runner: module count did not return to baseline\n");

        return 21;
    }

    if (run("/bin/modstat", stat_argv) == 0) {
        printf("runner: modstat of a closed module unexpectedly succeeded\n");

        return 22;
    }

    printf("runner: lifecycle module closed\n");

    if (run_as("/bin/modopen", lifecycle_open_argv, UNPRIVILEGED_UID) != 1) {
        printf("runner: non-root modopen was not denied\n");

        return 23;
    }

    if (run_as("/bin/modclose", close_argv, UNPRIVILEGED_UID) != 1) {
        printf("runner: non-root modclose was not denied\n");

        return 24;
    }

    printf("SYSUTILS_OK\n");

    return 0;
}
