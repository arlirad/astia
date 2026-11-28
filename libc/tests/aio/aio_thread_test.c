#define _POSIX_C_SOURCE 200809L

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__GNUC__)
#include_next <pthread.h>
#else
#include <pthread.h>
#endif

enum { aio_slot_count = 128 };

static int failures;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

struct submit_context {
    struct aiocb*     cb;
    pthread_barrier_t submitted;
    pthread_barrier_t queried;
    int               result;
};

static void* submit_worker(void* argument) {
    struct submit_context* context = argument;

    context->result = aio_write(context->cb);

    (void) pthread_barrier_wait(&context->submitted);
    (void) pthread_barrier_wait(&context->queried);

    return NULL;
}

int main(void) {
    const char* path = "/tmp/astia_aio_thread_test.dat";

    char input[]              = "cross-thread";
    char output[sizeof input] = {0};

    struct aiocb          write_cb = {0};
    struct aiocb          bad_cb   = {0};
    struct submit_context context;

    pthread_t thread;

    int fd;

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0600);

    CHECK(fd >= 0);

    if (fd < 0)
        return failures != 0;

    pthread_barrier_init(&context.submitted, NULL, 2);
    pthread_barrier_init(&context.queried, NULL, 2);

    write_cb.aio_fildes = fd;
    write_cb.aio_buf    = input;
    write_cb.aio_nbytes = sizeof input - 1;

    context.cb     = &write_cb;
    context.result = -1;

    CHECK(pthread_create(&thread, NULL, submit_worker, &context) == 0);

    (void) pthread_barrier_wait(&context.submitted);

    CHECK(context.result == 0);
    CHECK(aio_error(&write_cb) == 0);
    CHECK(aio_cancel(fd, &write_cb) == AIO_ALLDONE);
    CHECK(aio_return(&write_cb) == (ssize_t) (sizeof input - 1));
    CHECK(aio_error(&write_cb) == EINVAL);

    (void) pthread_barrier_wait(&context.queried);

    CHECK(pthread_join(thread, NULL) == 0);

    bad_cb.aio_fildes = -1;
    bad_cb.aio_buf    = output;
    bad_cb.aio_nbytes = 1;

    context.cb     = &bad_cb;
    context.result = -1;

    CHECK(pthread_create(&thread, NULL, submit_worker, &context) == 0);

    (void) pthread_barrier_wait(&context.submitted);

    CHECK(context.result == 0);
    CHECK(aio_error(&bad_cb) == EBADF);
    CHECK(aio_cancel(fd, &bad_cb) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(aio_return(&bad_cb) == -1 && errno == EBADF);
    CHECK(aio_error(&bad_cb) == EINVAL);

    (void) pthread_barrier_wait(&context.queried);

    CHECK(pthread_join(thread, NULL) == 0);

    {
        struct aiocb slots[aio_slot_count];
        struct aiocb reused = {0};

        size_t i;

        for (i = 0; i < aio_slot_count; ++i) {
            slots[i] = (struct aiocb){0};

            slots[i].aio_fildes = fd;
            slots[i].aio_buf    = input;
            slots[i].aio_nbytes = 1;

            CHECK(aio_write(&slots[i]) == 0);
        }

        reused.aio_fildes = fd;
        reused.aio_buf    = input;
        reused.aio_nbytes = 1;

        errno = 0;

        CHECK(aio_write(&reused) == -1 && errno == EAGAIN);

        for (i = 0; i < aio_slot_count; ++i) {
            CHECK(aio_error(&slots[i]) == 0);
            CHECK(aio_return(&slots[i]) == 1);
        }

        CHECK(aio_write(&reused) == 0);
        CHECK(aio_error(&reused) == 0);
        CHECK(aio_return(&reused) == 1);
    }

    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);

    return failures != 0;
}
