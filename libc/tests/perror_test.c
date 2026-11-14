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

static void test_message(const char* prefix, int error, const char* expected) {
    char output[128];
    int  after_errno;
    long length;

    length = capture(prefix, error, output, sizeof output, &after_errno);
    CHECK(length == (long) strlen(expected));
    CHECK(length >= 0 && memcmp(output, expected, strlen(expected)) == 0);
    CHECK(after_errno == error);
}

static void test_write_error(void) {
    int saved;

    saved = dup(2);
    CHECK(saved >= 0);
    if (saved < 0)
        return;
    close(2);
    clearerr(stderr);
    errno = EINVAL;
    perror("write");
    CHECK(errno == EBADF);
    CHECK(ferror(stderr) != 0);
    CHECK(fputs("ignored", stderr) == EOF);
    CHECK(errno == EBADF);
    CHECK(dup2(saved, 2) == 2);
    close(saved);
    clearerr(stderr);
}

int main(void) {
    void (*perror_api)(const char*) = perror;

    CHECK(perror_api != NULL);
    test_message("open", ENOENT, "open: No such file or directory\n");
    test_message("", EINVAL, "Invalid argument\n");
    test_message(NULL, ENOTTY, "Inappropriate ioctl for device\n");
    test_write_error();

    if (fails)
        printf("perror_test: %d failure(s)\n", fails);
    return fails != 0;
}
