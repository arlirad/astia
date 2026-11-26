#define _POSIX_C_SOURCE 200809L

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern ssize_t pread(int fd, void* buffer, size_t length, off_t offset);

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main(void) {
    const char* path = "/tmp/astia_aio_test.dat";

    char output[8] = {0};
    char input[]   = "aio";

    struct aiocb        write_cb        = {0};
    struct aiocb        read_cb         = {0};
    struct aiocb        bad_cb          = {0};
    struct aiocb        offset_cb       = {0};
    struct aiocb        list_write_cb   = {0};
    struct aiocb        list_read_cb    = {0};
    struct aiocb        list_bad_cb     = {0};
    struct aiocb        list_invalid_cb = {0};
    struct aiocb        list_signal_cb  = {0};
    const struct aiocb* list[1];
    struct aiocb*       lio_list[3];
    struct sigevent     signal_event  = {0};
    struct sigevent     ignored_event = {0};

    int fd;

    CHECK(O_DSYNC != O_SYNC);
    CHECK((O_SYNC & O_DSYNC) == O_DSYNC);
    CHECK((O_SYNC & ~(O_DSYNC | 0x100000)) == 0);
    CHECK((O_DSYNC & (O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NONBLOCK |
                      O_CLOEXEC | O_NOCTTY | O_DIRECTORY)) == 0);

    {
        unsigned char append0[512], append1[1024], append2[1536], append_check[3072];

        struct aiocb append_cb[3] = {0};

        const size_t append_sizes[3] = {sizeof append0, sizeof append1, sizeof append2};

        unsigned char* append_buffers[3] = {append0, append1, append2};

        int append_fd = open("/tmp/astia_aio_append_test.dat",
                             O_CREAT | O_TRUNC | O_APPEND | O_RDWR, 0600);

        CHECK(append_fd >= 0);

        if (append_fd >= 0) {
            for (int i = 0; i < 3; ++i) {
                memset(append_buffers[i], i, append_sizes[i]);

                append_cb[i].aio_fildes = append_fd;
                append_cb[i].aio_buf    = append_buffers[i];
                append_cb[i].aio_nbytes = append_sizes[i];

                CHECK(aio_write(&append_cb[i]) == 0);
            }

            for (int i = 0; i < 3; ++i) {
                CHECK(aio_error(&append_cb[i]) == 0);
                CHECK(aio_return(&append_cb[i]) == (ssize_t) append_sizes[i]);
            }

            CHECK(lseek(append_fd, 0, SEEK_SET) == 0);
            CHECK(read(append_fd, append_check, sizeof append_check) ==
                  sizeof append_check);
            CHECK(memcmp(append_check, append0, sizeof append0) == 0);
            CHECK(memcmp(append_check + sizeof append0, append1, sizeof append1) == 0);
            CHECK(memcmp(append_check + sizeof append0 + sizeof append1, append2,
                         sizeof append2) == 0);
            CHECK(close(append_fd) == 0);
            CHECK(unlink("/tmp/astia_aio_append_test.dat") == 0);
        }
    }

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);

    CHECK(fd >= 0);

    if (fd < 0)
        return failures != 0;

    write_cb.aio_fildes = fd;
    write_cb.aio_buf    = input;
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
    read_cb.aio_buf    = output;
    read_cb.aio_nbytes = sizeof input - 1;
    read_cb.aio_offset = 0;

    CHECK(aio_read(&read_cb) == 0);

    list[0] = &read_cb;

    CHECK(aio_suspend(list, 1, NULL) == 0);
    CHECK(aio_error(&read_cb) == 0);
    CHECK(aio_return(&read_cb) == (ssize_t) (sizeof input - 1));
    CHECK(memcmp(output, input, sizeof input - 1) == 0);

    {
        struct aiocb sync_write_cb = {0};
        struct aiocb sync_cb       = {0};

        sync_write_cb.aio_fildes = fd;
        sync_write_cb.aio_buf    = input;
        sync_write_cb.aio_nbytes = sizeof input - 1;
        sync_write_cb.aio_offset = 0;

        CHECK(aio_write(&sync_write_cb) == 0);

        sync_cb.aio_fildes  = fd;
        sync_cb.aio_buf     = NULL;
        sync_cb.aio_nbytes  = (size_t) -1;
        sync_cb.aio_reqprio = -1;
        sync_cb.aio_offset  = -1;

        CHECK(aio_fsync(O_SYNC, &sync_cb) == 0);
        CHECK(aio_error(&sync_cb) == 0);
        CHECK(aio_return(&sync_cb) == 0);
        CHECK(aio_error(&sync_write_cb) == 0);
        CHECK(aio_return(&sync_write_cb) == (ssize_t) (sizeof input - 1));

        memset(&sync_cb, 0, sizeof sync_cb);

        sync_cb.aio_fildes = fd;

        CHECK(aio_fsync(O_DSYNC, &sync_cb) == 0);
        CHECK(aio_error(&sync_cb) == 0);
        CHECK(aio_return(&sync_cb) == 0);

        errno = 0;

        CHECK(aio_fsync(-1, &sync_cb) == -1 && errno == EINVAL);

        errno = 0;

        CHECK(aio_fsync(O_SYNC, NULL) == -1 && errno == EINVAL);

        sync_cb.aio_fildes = -1;
        errno              = 0;

        CHECK(aio_fsync(O_SYNC, &sync_cb) == -1 && errno == EBADF);
    }

    bad_cb.aio_fildes = -1;
    bad_cb.aio_buf    = output;
    bad_cb.aio_nbytes = 1;

    CHECK(aio_read(&bad_cb) == 0);
    CHECK(aio_error(&bad_cb) == EBADF);

    errno = 0;

    CHECK(aio_return(&bad_cb) == -1 && errno == EBADF);

    offset_cb.aio_fildes = fd;
    offset_cb.aio_buf    = input;
    offset_cb.aio_nbytes = 1;
    offset_cb.aio_offset = -1;
    errno                = 0;

    CHECK(aio_write(&offset_cb) == -1 && errno == EINVAL);

    CHECK(aio_cancel(-1, NULL) == -1 && errno == EBADF);
    CHECK(aio_suspend(NULL, 0, NULL) == -1 && errno == EINVAL);

    list_write_cb.aio_fildes       = fd;
    list_write_cb.aio_buf          = input;
    list_write_cb.aio_nbytes       = sizeof input - 1;
    list_write_cb.aio_offset       = 0;
    list_write_cb.aio_lio_opcode   = LIO_WRITE;
    ignored_event.sigev_notify     = SIGEV_SIGNAL;
    lio_list[0]                    = &list_write_cb;
    lio_list[1]                    = NULL;
    lio_list[2]                    = &list_invalid_cb;
    list_invalid_cb.aio_lio_opcode = LIO_NOP;

    CHECK(lio_listio(LIO_WAIT, lio_list, 3, &ignored_event) == 0);
    CHECK(aio_error(&list_write_cb) == 0);
    CHECK(aio_return(&list_write_cb) == (ssize_t) (sizeof input - 1));

    list_read_cb.aio_fildes     = fd;
    list_read_cb.aio_buf        = output;
    list_read_cb.aio_nbytes     = sizeof input - 1;
    list_read_cb.aio_offset     = 0;
    list_read_cb.aio_lio_opcode = LIO_READ;
    lio_list[0]                 = &list_read_cb;

    CHECK(lio_listio(LIO_NOWAIT, lio_list, 1, &ignored_event) == -1 && errno == ENOTSUP);

    signal_event.sigev_notify = SIGEV_NONE;

    CHECK(lio_listio(LIO_NOWAIT, lio_list, 1, &signal_event) == 0);
    CHECK(aio_error(&list_read_cb) == 0);
    CHECK(aio_return(&list_read_cb) == (ssize_t) (sizeof input - 1));

    errno = 0;

    CHECK(lio_listio(-1, lio_list, 1, NULL) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(lio_listio(LIO_WAIT, lio_list, 0, NULL) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(lio_listio(LIO_WAIT, NULL, 1, NULL) == -1 && errno == EINVAL);

    list_write_cb.aio_lio_opcode   = LIO_WRITE;
    list_invalid_cb.aio_lio_opcode = -1;
    lio_list[0]                    = &list_write_cb;
    lio_list[1]                    = &list_invalid_cb;
    errno                          = 0;

    CHECK(lio_listio(LIO_WAIT, lio_list, 2, NULL) == -1 && errno == EIO);
    CHECK(aio_return(&list_write_cb) == (ssize_t) (sizeof input - 1));
    CHECK(aio_error(&list_invalid_cb) == EINVAL);

    list_bad_cb.aio_fildes     = fd;
    list_bad_cb.aio_buf        = NULL;
    list_bad_cb.aio_nbytes     = 1;
    list_bad_cb.aio_lio_opcode = LIO_WRITE;
    lio_list[0]                = &list_bad_cb;
    errno                      = 0;

    CHECK(lio_listio(LIO_WAIT, lio_list, 1, NULL) == -1 && errno == EINVAL);

    list_signal_cb.aio_fildes                = fd;
    list_signal_cb.aio_buf                   = input;
    list_signal_cb.aio_nbytes                = sizeof input - 1;
    list_signal_cb.aio_lio_opcode            = LIO_WRITE;
    list_signal_cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    lio_list[0]                              = &list_signal_cb;
    errno                                    = 0;

    CHECK(lio_listio(LIO_WAIT, lio_list, 1, NULL) == -1 && errno == ENOTSUP);

    CHECK(pread(fd, output, sizeof input - 1, 0) == (ssize_t) (sizeof input - 1));
    CHECK(close(fd) == 0);

    {
        struct aiocb closed_cb = {0};

        closed_cb.aio_fildes = fd;
        errno                = 0;

        CHECK(aio_fsync(O_SYNC, &closed_cb) == -1 && errno == EBADF);
    }

    CHECK(unlink(path) == 0);

    return failures != 0;
}
