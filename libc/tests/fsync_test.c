#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

int main(void) {
    const char* path = "/tmp/astia-fsync-test";
    int         fd;
    int         pipe_fds[2];
    int         pipe_result;

    (void) unlink(path);
    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(write(fd, "sync", 4) == 4);
        CHECK(fsync(fd) == 0);
        CHECK(close(fd) == 0);
    }
    fd = open(path, O_RDONLY, 0);
    CHECK(fd >= 0);
    if (fd >= 0) {
        CHECK(fsync(fd) == 0);
        CHECK(close(fd) == 0);
    }
    errno = 0;
    CHECK(fsync(-1) == -1 && errno == EBADF);
    pipe_result = pipe(pipe_fds);
    CHECK(pipe_result == 0);
    if (pipe_result == 0) {
        errno = 0;
        CHECK(fsync(pipe_fds[1]) == -1 && errno == EINVAL);
        CHECK(close(pipe_fds[0]) == 0);
        CHECK(close(pipe_fds[1]) == 0);
    }
    CHECK(unlink(path) == 0);
    return failures != 0;
}
