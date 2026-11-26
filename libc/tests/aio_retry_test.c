#define _POSIX_C_SOURCE 200809L

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int retry_result;
static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

long __astia_fio_aio_result(uint64_t tag, long* result) {
    (void) tag;
    if (retry_result == -EAGAIN)
        return -EAGAIN;
    *result = retry_result;
    return 1;
}

int main(void) {
    const char*         path     = "/tmp/astia_aio_retry_test.dat";
    char                byte     = 'x';
    struct aiocb        read_cb  = {0};
    struct aiocb        write_cb = {0};
    const struct aiocb* list[1];
    struct timespec     now = {0, 0};
    int                 fd;

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);
    CHECK(fd >= 0);
    if (fd < 0)
        return failures != 0;
    CHECK(write(fd, &byte, 1) == 1);

    read_cb.aio_fildes = fd;
    read_cb.aio_buf    = &byte;
    read_cb.aio_nbytes = 1;
    read_cb.aio_offset = 0;
    CHECK(aio_read(&read_cb) == 0);

    retry_result = -EAGAIN;
    CHECK(aio_error(&read_cb) == EINPROGRESS);
    retry_result = 7;
    CHECK(aio_error(&read_cb) == 0);
    CHECK(aio_return(&read_cb) == 7);

    write_cb.aio_fildes = fd;
    write_cb.aio_buf    = &byte;
    write_cb.aio_nbytes = 1;
    write_cb.aio_offset = 0;
    CHECK(aio_write(&write_cb) == 0);
    list[0] = &write_cb;

    retry_result = -EAGAIN;
    errno        = 0;
    CHECK(aio_suspend(list, 1, &now) == -1 && errno == EAGAIN);
    CHECK(aio_error(&write_cb) == EINPROGRESS);
    retry_result = 9;
    CHECK(aio_suspend(list, 1, NULL) == 0);
    CHECK(aio_return(&write_cb) == 9);

    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);
    return failures != 0;
}
