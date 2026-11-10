#pragma once

#include <sys/types.h>
#include <time.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef union sigval {
    int   sival_int;
    void* sival_ptr;
} sigval_t;

struct sigevent {
    int     sigev_notify;
    int     sigev_signo;
    sigval_t sigev_value;
    void (*sigev_notify_function)(sigval_t);
    void* sigev_notify_attributes;
};

#define SIGEV_NONE   0
#define SIGEV_SIGNAL 1
#define SIGEV_THREAD 2

struct aiocb {
    int            aio_fildes;
    int            aio_lio_opcode;
    int            aio_reqprio;
    volatile void* aio_buf;
    size_t         aio_nbytes;
    struct sigevent aio_sigevent;
    off_t          aio_offset;
};

#define LIO_READ   0
#define LIO_WRITE  1
#define LIO_NOP    2
#define LIO_NOWAIT 0
#define LIO_WAIT   1

#define AIO_CANCELED    0
#define AIO_NOTCANCELED 1
#define AIO_ALLDONE     2

int    aio_read(struct aiocb* aiocbp);
int    aio_write(struct aiocb* aiocbp);
int    lio_listio(int mode, struct aiocb* const list[], int nent,
                  struct sigevent* sig);
int    aio_error(const struct aiocb* aiocbp);
ssize_t aio_return(struct aiocb* aiocbp);
int    aio_suspend(const struct aiocb* const list[], int nent,
                   const struct timespec* timeout);
int    aio_cancel(int fildes, struct aiocb* aiocbp);

#ifdef __cplusplus
}
#endif
