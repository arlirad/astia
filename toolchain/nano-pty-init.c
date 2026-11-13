#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char* message) {
    (void) write(STDOUT_FILENO, message, strlen(message));
    return 1;
}

int main(void) {
    char* const editor_argv[] = {(char*) "/bin/nano", (char*) "-t",
                                 (char*) "/tmp/nano-smoke.txt", NULL};
    char* const editor_env[] = {(char*) "TERM=vt220", NULL};
    int         file;
    int         status;
    int         child;
    int         verified = 0;
    char        contents[64] = {0};
    long        count;
    int         seed_file = open("/tmp/nano-smoke.txt", O_CREAT | O_RDWR, 0600);
    if (seed_file >= 0)
        close(seed_file);
    (void) mkdir("/tmp", 0777);
#ifdef NANO_DIRECT_EXEC
    child = fork();
    if (child == 0) {
        execve(editor_argv[0], editor_argv, editor_env);
        _exit(127);
    }
#else
    child = spawnve(editor_argv[0], editor_argv, editor_env);
    if (child < 0)
        return fail("NANO_SMOKE_TTY_SETUP_FAIL\n");
#endif
    for (int attempt = 0; attempt < 60; ++attempt) {
        file = open("/tmp/nano-smoke.txt", O_RDONLY, 0);
        if (file >= 0) {
            count = read(file, contents, sizeof contents - 1);
            close(file);
            if (count >= 0 && strcmp(contents, "Astia nano smoke\n") == 0) {
                (void) write(STDOUT_FILENO, "NANO_SMOKE_OK\n", 14);
                verified = 1;
                break;
            }
        }
        sleep(1);
    }
    if (waitpid(child, &status, 0) < 0)
        return fail("NANO_SMOKE_WAIT_FAIL\n");
    if (!verified || status != 0) {
        char message[64];
        snprintf(message, sizeof message, "NANO_SMOKE_EDITOR_FAIL %d\n", status);
        return fail(message);
    }
#ifndef NANO_DIRECT_EXEC
    (void) tcsetpgrp(STDIN_FILENO, getpgrp());
#endif
    return 0;
}
