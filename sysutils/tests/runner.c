#include <astia/api/syscalls.h>

#include <sys/wait.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char* const environment[] = {(char*) "PATH=/bin", NULL};

static int run(const char* path, char* const argv[]) {
    const int pid = spawnve(path, argv, environment);
    if (pid < 0) {
        printf("runner: spawn %s failed\n", path);

        return -1;
    }

    int status = 0;

    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
        return -1;

    return WEXITSTATUS(status);
}

int main(void) {
    astia_module_record    records[ASTIA_MODULE_LIST_MAX] = {};
    astia_module_list_info info                           = {};

    char token[64];

    char* ls[]         = {(char*) "modls", NULL};
    char* stat_argv[]  = {(char*) "modstat", token, NULL};
    char* open_argv[]  = {(char*) "modopen", (char*) "/missing-module.o", NULL};
    char* close_argv[] = {(char*) "modclose", token, NULL};

    if (run("/bin/modls", ls) != 0) {
        printf("runner: modls failed\n");

        return 10;
    }

    if (astia_modls(records, ASTIA_MODULE_LIST_MAX, &info) < 0 || info.count == 0) {
        printf("runner: no published module to stat\n");

        return 11;
    }

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

    printf("SYSUTILS_OK\n");

    return 0;
}
