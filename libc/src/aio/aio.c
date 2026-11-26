#include <astia/api/syscalls.h>

#include "internal/file_port.h"

#include <aio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>

enum { aio_slots_max = 128 };

struct aio_slot {
    struct aiocb* cb;
    uint64_t      tag;
    long          result;
    int           complete;
    int           returned;
};

static _Thread_local struct aio_slot g_aio_slots[aio_slots_max];
static _Thread_local uint64_t        g_aio_host_tag;

static struct aio_slot* aio_find(const struct aiocb* cb) {
    size_t i;

    for (i = 0; i < aio_slots_max; ++i) {
        if (g_aio_slots[i].cb == cb && !g_aio_slots[i].returned)
            return &g_aio_slots[i];
    }

    return NULL;
}

static struct aio_slot* aio_free_slot(void) {
    size_t i;

    for (i = 0; i < aio_slots_max; ++i) {
        if (g_aio_slots[i].cb == NULL || g_aio_slots[i].returned)
            return &g_aio_slots[i];
    }

    return NULL;
}

static int aio_update(struct aio_slot* slot) {
    long result;
    long rc;

    if (slot->complete)
        return 1;

    rc = __astia_fio_aio_result(slot->tag, &result);
    if (rc < 0) {
        slot->result   = rc;
        slot->complete = 1;

        return 1;
    }

    if (rc == 0)
        return 0;

    slot->result   = result;
    slot->complete = 1;

    return 1;
}

static struct aio_slot* aio_begin(const struct aiocb* cb) {
    struct aio_slot* slot;

    if (aio_find(cb) != NULL) {
        errno = EINVAL;

        return NULL;
    }

    slot = aio_free_slot();
    if (slot == NULL)
        errno = EAGAIN;

    return slot;
}

static void aio_commit(struct aio_slot* slot, struct aiocb* cb, uint64_t tag) {
    *slot = (struct aio_slot){cb, tag, 0, 0, 0};
}

static int aio_submit(struct aiocb* cb, int write) {
    struct aio_slot* slot;
    uint64_t         tag = 0;

    long rc;

    if (cb == NULL || cb->aio_nbytes > UINT32_MAX || cb->aio_offset < 0 ||
        cb->aio_reqprio != 0 || (cb->aio_nbytes != 0 && cb->aio_buf == NULL)) {
        errno = EINVAL;

        return -1;
    }

    slot = aio_begin(cb);
    if (slot == NULL)
        return -1;

    rc = __astia_fio_aio_submit(cb->aio_fildes, (uint64_t) cb->aio_offset,
                                (void*) cb->aio_buf, cb->aio_nbytes, write, &tag);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    aio_commit(slot, cb, tag);

    return 0;
}

int aio_read(struct aiocb* aiocbp) {
    return aio_submit(aiocbp, 0);
}

int aio_write(struct aiocb* aiocbp) {
    return aio_submit(aiocbp, 1);
}

int aio_fsync(int op, struct aiocb* aiocbp) {
    struct aio_slot* slot;
    uint64_t         tag = 0;

    long rc;

    if ((op != O_SYNC && op != O_DSYNC) || aiocbp == NULL) {
        errno = EINVAL;

        return -1;
    }

    if (fcntl(aiocbp->aio_fildes, F_GETFD) < 0)
        return -1;

    slot = aio_begin(aiocbp);
    if (slot == NULL)
        return -1;

    rc = __astia_fio_aio_fsync(aiocbp->aio_fildes, &tag);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    aio_commit(slot, aiocbp, tag);

    return 0;
}

static int aio_event_supported(const struct sigevent* event) {
    return event == NULL || event->sigev_notify == SIGEV_NONE;
}

static void aio_wait_all(struct aiocb* const list[], size_t count) {
    size_t completed = 0;
    size_t i;

    while (completed < count) {
        completed = 0;

        for (i = 0; i < count; ++i) {
            if (aio_error(list[i]) != EINPROGRESS)
                ++completed;
        }

        if (completed < count)
            (void) astia_yield();
    }
}

int lio_listio(int mode, struct aiocb* const list[], int nent, struct sigevent* sig) {
    struct aiocb* submitted[aio_slots_max];

    size_t submitted_count = 0;
    int    failure         = 0;
    int    i;

    if ((mode != LIO_WAIT && mode != LIO_NOWAIT) || nent <= 0 || nent > aio_slots_max ||
        list == NULL) {
        errno = EINVAL;

        return -1;
    }

    if (mode == LIO_NOWAIT && !aio_event_supported(sig)) {
        errno = ENOTSUP;

        return -1;
    }

    for (i = 0; i < nent; ++i) {
        struct aiocb* cb = list[i];

        int rc;

        if (cb == NULL || cb->aio_lio_opcode == LIO_NOP)
            continue;

        if (cb->aio_lio_opcode != LIO_READ && cb->aio_lio_opcode != LIO_WRITE) {
            if (failure == 0)
                failure = EIO;

            continue;
        }

        if (!aio_event_supported(&cb->aio_sigevent)) {
            if (failure == 0)
                failure = ENOTSUP;

            continue;
        }

        rc = cb->aio_lio_opcode == LIO_READ ? aio_read(cb) : aio_write(cb);
        if (rc < 0) {
            if (failure == 0)
                failure = errno;

            continue;
        }

        submitted[submitted_count++] = cb;
    }

    if (mode == LIO_WAIT && submitted_count != 0)
        aio_wait_all(submitted, submitted_count);

    if (failure != 0) {
        errno = failure;

        return -1;
    }

    return 0;
}

int aio_error(const struct aiocb* aiocbp) {
    struct aio_slot* slot = aio_find(aiocbp);
    if (slot == NULL)
        return EINVAL;

    if (!aio_update(slot))
        return EINPROGRESS;

    return slot->result < 0 ? (int) -slot->result : 0;
}

ssize_t aio_return(struct aiocb* aiocbp) {
    struct aio_slot* slot = aio_find(aiocbp);

    long result;

    if (slot == NULL || !aio_update(slot)) {
        errno = EINVAL;

        return -1;
    }

    result         = slot->result;
    slot->returned = 1;
    slot->cb       = NULL;

    if (result < 0) {
        errno = (int) -result;

        return -1;
    }

    return (ssize_t) result;
}

static uint64_t aio_now(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;

    if (now.tv_sec < 0 || (uint64_t) now.tv_sec > UINT64_MAX / 1000000000ull)
        return UINT64_MAX;

    return (uint64_t) now.tv_sec * 1000000000ull + (uint64_t) now.tv_nsec;
}

static uint64_t aio_deadline(const struct timespec* timeout, uint64_t now) {
    uint64_t seconds;
    uint64_t delta;

    if (timeout == NULL)
        return UINT64_MAX;

    seconds = (uint64_t) timeout->tv_sec;

    if (seconds > UINT64_MAX / 1000000000ull)
        return UINT64_MAX;

    if (UINT64_MAX - seconds * 1000000000ull < (uint64_t) timeout->tv_nsec)
        return UINT64_MAX;

    delta = seconds * 1000000000ull + (uint64_t) timeout->tv_nsec;

    return UINT64_MAX - now < delta ? UINT64_MAX : now + delta;
}

int aio_suspend(const struct aiocb* const list[], int nent,
                const struct timespec* timeout) {
    uint64_t deadline;

    int have = 0;
    int i;

    if (list == NULL || nent <= 0 ||
        (timeout != NULL && (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                             timeout->tv_nsec >= 1000000000L))) {
        errno = EINVAL;

        return -1;
    }

    deadline = aio_deadline(timeout, aio_now());

    for (;;) {
        for (i = 0; i < nent; ++i) {
            struct aio_slot* slot;

            if (list[i] == NULL)
                continue;

            slot = aio_find(list[i]);
            if (slot == NULL)
                continue;

            have = 1;

            if (aio_update(slot))
                return 0;
        }

        if (!have) {
            errno = EINVAL;

            return -1;
        }

        if (timeout != NULL && aio_now() >= deadline) {
            errno = EAGAIN;

            return -1;
        }

        (void) astia_yield();
    }
}

int aio_cancel(int fildes, struct aiocb* aiocbp) {
    int    active = 0;
    int    done   = 0;
    size_t i;

    if (fildes < 0) {
        errno = EBADF;

        return -1;
    }

    if (aiocbp != NULL) {
        struct aio_slot* slot = aio_find(aiocbp);

        if (slot == NULL || slot->cb->aio_fildes != fildes) {
            errno = EINVAL;

            return -1;
        }

        if (aio_update(slot))
            return AIO_ALLDONE;

        return AIO_NOTCANCELED;
    }

    for (i = 0; i < aio_slots_max; ++i) {
        struct aio_slot* slot = &g_aio_slots[i];

        if (slot->cb == NULL || slot->returned || slot->cb->aio_fildes != fildes)
            continue;

        if (aio_update(slot))
            ++done;
        else
            ++active;
    }

    if (active != 0)
        return AIO_NOTCANCELED;

    (void) done;

    return AIO_ALLDONE;
}

static _Thread_local struct {
    uint64_t tag;
    long     result;
    int      used;
} g_host_results[aio_slots_max];

static long aio_host_record(long result, uint64_t* tag) {
    size_t i;

    for (i = 0; i < aio_slots_max; ++i) {
        if (!g_host_results[i].used) {
            g_host_results[i].used   = 1;
            g_host_results[i].tag    = ++g_aio_host_tag;
            g_host_results[i].result = result;
            *tag                     = g_host_results[i].tag;

            return 0;
        }
    }

    return -EAGAIN;
}

long __attribute__((weak)) __astia_fio_aio_submit(int fd, uint64_t off, void* buf,
                                                  size_t n, int write, uint64_t* tag) {
    long result =
        write ? __astia_fio_write(fd, off, buf, n) : __astia_fio_read(fd, off, buf, n);

    return aio_host_record(result, tag);
}

long __attribute__((weak)) __astia_fio_aio_fsync(int fd, uint64_t* tag) {
    return aio_host_record(__astia_fio_fsync(fd), tag);
}

long __attribute__((weak)) __astia_fio_aio_result(uint64_t tag, long* result) {
    size_t i;

    for (i = 0; i < aio_slots_max; ++i) {
        if (g_host_results[i].used && g_host_results[i].tag == tag) {
            *result                = g_host_results[i].result;
            g_host_results[i].used = 0;

            return 1;
        }
    }

    return 0;
}
