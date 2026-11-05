#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static int run_test(const char* name, const char* path) {
    char* argv[] = {(char*) path, NULL};
    int   status  = 0;
    int   child   = spawnve(path, argv, NULL);
    if (child < 0) {
        printf("POSIX_TEST %s FAIL spawn errno=%d\n", name, errno);
        return 1;
    }
    if (waitpid(child, &status, 0) != child) {
        printf("POSIX_TEST %s FAIL wait errno=%d\n", name, errno);
        return 1;
    }
    if (!WIFEXITED(status)) {
        printf("POSIX_TEST %s FAIL signal=%d\n", name, WTERMSIG(status));
        return 1;
    }
    const int result = WEXITSTATUS(status);
    if (result == 0) {
        printf("POSIX_TEST %s PASS rc=0\n", name);
        return 0;
    }
    if (result == 4) {
        printf("POSIX_TEST %s UNSUPPORTED rc=4\n", name);
        return 0;
    }
    printf("POSIX_TEST %s FAIL rc=%d\n", name, result);
    return 1;
}

int main(void) {
    int failed = 0;
    FILE* manifest = fopen("/etc/posix-tests", "r");
    if (manifest == NULL)
        return 1;
    char line[256];
    char name[96];
    char path[160];
    while (fgets(line, sizeof(line), manifest) != NULL) {
        if (sscanf(line, "%95s %159s", name, path) == 2)
            failed |= run_test(name, path);
    }
    fclose(manifest);
    return failed;
}
