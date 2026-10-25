#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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
