#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                               \
        }                                                          \
    } while (0)

static long capture(const char* prefix, int error, char* output, size_t capacity,
                    int* after_errno) {
    int  fds[2];
    int  saved;
    long length;

    if (pipe(fds) != 0)
        return -1;
    saved = dup(2);
    if (saved < 0 || dup2(fds[1], 2) < 0)
        return -1;
    close(fds[1]);

    errno = error;
    perror(prefix);
    *after_errno = errno;
    length = read(fds[0], output, capacity);
    dup2(saved, 2);
    close(saved);
    close(fds[0]);
    return length;
}

int main(void) {
    char output[128];
    int  after_errno;
    long length;

    length = capture("open", ENOENT, output, sizeof output, &after_errno);
    CHECK(length == (long) strlen("open: No such file or directory\n"));
    CHECK(length >= 0 &&
          memcmp(output, "open: No such file or directory\n", strlen("open: No such file or directory\n")) == 0);
    CHECK(after_errno == ENOENT);

    length = capture("", EINVAL, output, sizeof output, &after_errno);
    CHECK(length == (long) strlen("Invalid argument\n"));
    CHECK(length >= 0 && memcmp(output, "Invalid argument\n", strlen("Invalid argument\n")) == 0);
    CHECK(after_errno == EINVAL);

    if (fails)
        printf("perror_test: %d failure(s)\n", fails);
    return fails != 0;
}
