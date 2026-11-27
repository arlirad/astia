#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int fails;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

int main(void) {
    const char* path = "astia_creat_test_file";

    struct stat status;

    char buffer[8];
    int  fd;

    (void) unlink(path);

    fd = creat(path, 0600);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(write(fd, "abc", 3) == 3);

        errno = 0;

        CHECK(read(fd, buffer, sizeof buffer) == -1 && errno == EBADF);
        CHECK(close(fd) == 0);
    }

    CHECK(stat(path, &status) == 0 && S_ISREG(status.st_mode) && status.st_size == 3);
    CHECK((status.st_mode & 0777) == 0600);

    fd = creat(path, 0600);

    CHECK(fd >= 0);

    if (fd >= 0)
        CHECK(close(fd) == 0);

    CHECK(stat(path, &status) == 0 && status.st_size == 0);
    CHECK(unlink(path) == 0);

    return fails != 0;
}
