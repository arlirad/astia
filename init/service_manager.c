#include "service_manager.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

enum { SERVICE_LINE = 256, SERVICE_MAX = 32 };

static char* trim(char* text) {
    char* end;
    while (*text == ' ' || *text == '\t') ++text;
    end = text + strlen(text);
    while (end != text && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) --end;
    *end = '\0';
    return text;
}

int astia_service_manager(const char* rc_path) {
    FILE* file = fopen(rc_path == NULL ? "/etc/rc" : rc_path, "r");
    char line[SERVICE_LINE];
    int children[SERVICE_MAX];
    int count = 0;
    int failures = 0;
    if (file == NULL) return errno == ENOENT ? 0 : -1;
    while (fgets(line, sizeof line, file) != NULL) {
        char* path = trim(line);
        char* argv[2];
        int pid;
        if (*path == '\0' || *path == '#') continue;
        /* A service definition is exactly one absolute executable path. */
        if (*path != '/' || strchr(path, ' ') != NULL || strchr(path, '\t') != NULL || count == SERVICE_MAX) {
            ++failures;
            continue;
        }
        argv[0] = path; argv[1] = NULL;
        pid = spawnve(path, argv, NULL);
        if (pid < 0) { ++failures; continue; }
        /* spawnve() creates a fresh process/session group for the child.  Do
         * not call setpgid() here: the child is a session leader, so that
         * call is rejected by POSIX and would turn every valid service into a
         * startup failure. */
        children[count++] = pid;
    }
    fclose(file);
    while (count != 0) {
        int status;
        const int pid = children[count - 1];
        if (waitpid(pid, &status, 0) != pid) return -1;
        --count;
        if (status != 0) ++failures;
    }
    return failures == 0 ? 0 : -1;
}
