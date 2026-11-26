#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main(void) {
    char name[] = "/tmp/astia-lseek-XXXXXX";
    char byte;
    int  fd = mkstemp(name);
    int  copy;
    int  pipe_fds[2];

    CHECK(fd >= 0);

    if (fd < 0)
        return 1;

    CHECK(write(fd, "abcdef", 6) == 6);
    CHECK(lseek(fd, 2, SEEK_SET) == 2);
    CHECK(read(fd, &byte, 1) == 1 && byte == 'c');
    CHECK(lseek(fd, 2, SEEK_CUR) == 5);
    CHECK(lseek(fd, -1, SEEK_END) == 5);

    copy = dup(fd);

    CHECK(copy >= 0);

    if (copy >= 0) {
        CHECK(lseek(copy, 1, SEEK_SET) == 1);
        CHECK(lseek(fd, 1, SEEK_CUR) == 2);
        CHECK(close(copy) == 0);
    }

    errno = 0;

    CHECK(lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(lseek(fd, 0, 99) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(lseek(-1, 0, SEEK_SET) == -1 && errno == EBADF);
    CHECK(pipe(pipe_fds) == 0);

    errno = 0;

    CHECK(lseek(pipe_fds[0], 0, SEEK_SET) == -1 && errno == ESPIPE);
    CHECK(close(pipe_fds[0]) == 0);
    CHECK(close(pipe_fds[1]) == 0);
    CHECK(close(fd) == 0);
    CHECK(unlink(name) == 0);

    return failures != 0;
}
