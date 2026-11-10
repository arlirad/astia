#define _POSIX_C_SOURCE 200809L

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ssize_t pread(int fd, void* buffer, size_t length, off_t offset);

static int failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures; \
        } \
    } while (0)

int main(void) {
    const char* path = "/tmp/astia_aio_test.dat";
    char        output[8] = {0};
    char        input[] = "aio";
    struct aiocb write_cb = {0};
    struct aiocb read_cb = {0};
    struct aiocb bad_cb = {0};
    struct aiocb offset_cb = {0};
    const struct aiocb* list[1];
    int fd;

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(fd >= 0);
    if (fd < 0)
        return failures != 0;

    write_cb.aio_fildes = fd;
    write_cb.aio_buf = input;
    write_cb.aio_nbytes = sizeof input - 1;
    CHECK(aio_write(&write_cb) == 0);
    list[0] = &write_cb;
    CHECK(aio_suspend(list, 1, NULL) == 0);
    CHECK(aio_error(&write_cb) == 0);
    CHECK(aio_cancel(fd, &write_cb) == AIO_ALLDONE);
    CHECK(aio_return(&write_cb) == (ssize_t) (sizeof input - 1));
    errno = 0;
    CHECK(aio_return(&write_cb) == -1 && errno == EINVAL);

    read_cb.aio_fildes = fd;
    read_cb.aio_buf = output;
    read_cb.aio_nbytes = sizeof input - 1;
    read_cb.aio_offset = 0;
    CHECK(aio_read(&read_cb) == 0);
    list[0] = &read_cb;
    CHECK(aio_suspend(list, 1, NULL) == 0);
    CHECK(aio_error(&read_cb) == 0);
    CHECK(aio_return(&read_cb) == (ssize_t) (sizeof input - 1));
    CHECK(memcmp(output, input, sizeof input - 1) == 0);

    bad_cb.aio_fildes = -1;
    bad_cb.aio_buf = output;
    bad_cb.aio_nbytes = 1;
    CHECK(aio_read(&bad_cb) == 0);
    CHECK(aio_error(&bad_cb) == EBADF);
    errno = 0;
    CHECK(aio_return(&bad_cb) == -1 && errno == EBADF);

    offset_cb.aio_fildes = fd;
    offset_cb.aio_buf = input;
    offset_cb.aio_nbytes = 1;
    offset_cb.aio_offset = -1;
    errno = 0;
    CHECK(aio_write(&offset_cb) == -1 && errno == EINVAL);

    CHECK(aio_cancel(-1, NULL) == -1 && errno == EBADF);
    CHECK(aio_suspend(NULL, 0, NULL) == -1 && errno == EINVAL);
    CHECK(pread(fd, output, sizeof input - 1, 0) == (ssize_t) (sizeof input - 1));
    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);
    return failures != 0;
}
