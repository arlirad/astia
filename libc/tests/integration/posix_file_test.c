#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
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
    char name[] = "/tmp/astia-posix-XXXXXX";

    struct stat value;

    int fd = mkstemp(name);

    CHECK(fd >= 0);

    if (fd >= 0) {
        CHECK(ftruncate(fd, 4096) == 0);
        CHECK(fstat(fd, &value) == 0 && value.st_size == 4096);
        CHECK(ftruncate(fd, 7) == 0);
        CHECK(fstat(fd, &value) == 0 && value.st_size == 7);
        CHECK(close(fd) == 0);
        CHECK(unlink(name) == 0);
    }

    CHECK(sysconf(_SC_PAGESIZE) == 4096);
    CHECK(sysconf(_SC_MAPPED_FILES) > 0);
    CHECK(_POSIX_MEMORY_PROTECTION == 200809L);
    CHECK(sysconf(_SC_MEMORY_PROTECTION) == 200809L);
    CHECK(sysconf(_SC_CLK_TCK) == 100);
    CHECK(sysconf(_SC_OPEN_MAX) == 64);
    CHECK(sysconf(_SC_CHILD_MAX) == 64);
    CHECK(sysconf(_SC_CLOCK_SELECTION) == 1);
    CHECK(sysconf(_SC_MONOTONIC_CLOCK) == 1);
    CHECK(sysconf(_SC_CPUTIME) == -1);
    CHECK(sysconf(_SC_THREAD_PROCESS_SHARED) == -1);

    errno = 0;

    CHECK(sysconf(-1) == -1 && errno == EINVAL);

    return failures != 0;
}
