#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main(void) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    CHECK(write(fds[1], "hello", 5) == 5);
    close(fds[1]);
    char buffer[8] = {0};
    CHECK(read(fds[0], buffer, 5) == 5);
    CHECK(strcmp(buffer, "hello") == 0);
    CHECK(read(fds[0], buffer, 1) == 0);
    close(fds[0]);

    /* A child must release its copied writer before exec.  This is the
       native fork/exec shape used by shell pipelines; read() must observe
       EOF without waiting for the unrelated exec'ed image to exit. */
    CHECK(pipe(fds) == 0);
    int child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char* const argv[] = {"/bin/true", NULL};
        close(fds[0]);
        close(fds[1]);
        execve("/bin/true", argv, NULL);
        _Exit(127);
    }
    close(fds[1]);
    CHECK(read(fds[0], buffer, 1) == 0);
    CHECK(waitpid(child, NULL, 0) == child);
    close(fds[0]);

    const char* path = "/tmp/astia_pipeline_test";
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0);
    CHECK(dup2(fd, 1) == 1);
    CHECK(write(1, "one\n", 4) == 4);
    close(fd);
    fd = open(path, O_WRONLY | O_APPEND);
    CHECK(fd >= 0);
    CHECK(write(fd, "two\n", 4) == 4);
    close(fd);
    fd = open(path, O_RDONLY);
    char result[16] = {0};
    CHECK(fd >= 0 && read(fd, result, sizeof result - 1) == 8);
    CHECK(strcmp(result, "one\ntwo\n") == 0);
    close(fd);
    unlink(path);
    if (fails) printf("pipeline_test: %d failure(s)\n", fails);
    return fails != 0;
}
