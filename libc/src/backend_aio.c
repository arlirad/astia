#include "file_port.h"
#include <astia/api/aio.h>

#include <string.h>

/*
 * astia libc — the file port over the async I/O ring.
 *
 * The ABI has no synchronous read/write calls: every blocking primitive
 * is an SQE on an I/O ring.  Synchronous behavior is therefore built
 * here, one operation at a time:
 *
 *   fill SQE -> publish -> one astia_io_enter() that both kicks the
 *   kernel and waits -> reap the CQE by its user_data tag -> return
 *   cqe.result unchanged (negative = -errno: the port speaks the ABI's
 *   convention verbatim, file.c folds it into errno).
 *
 * The ring is set up lazily on the first file call and shared process
 * wide.  Single producer by ABI — concurrent users would need the
 * external lock (astia_futex) that arrives with threads; until then all
 * stdio calls are serialized by the same "not thread-safe" rule as FILE
 * itself.
 *
 * This translation unit only ever *executes* on astia (astia_raw6 is a
 * real trap there).  Host test binaries link tests/backend_host.c
 * instead; the file still compiles everywhere so the ABI never rots
 * silently — that compile is its check until a kernel-side ring exists
 * to run against.
 */

enum { RING_ENTRIES = 64 };

ASTIA_STATIC_ASSERT(FIO_OFF_APPEND == OFF_APPEND,
                    "port append offset must equal ABI OFF_APPEND");
ASTIA_STATIC_ASSERT(sizeof(struct iovec) == sizeof(astia_iovec) &&
                        offsetof(struct iovec, iov_base) == offsetof(astia_iovec, base) &&
                        offsetof(struct iovec, iov_len) == offsetof(astia_iovec, len),
                    "port iovec must be the ABI's astia_iovec");

static astia_rings* g_ring;
static uint64_t     g_tag;
static uint64_t     g_inflight; /* 0: none.  Tag of an operation whose kick
                                   failed *after* publish: its submission
                                   state is unknown, so it must be settled
                                   before any new SQE is written —
                                   otherwise the next enter could consume
                                   the stale entry and wait on a completion
                                   that never comes, or an append could be
                                   applied twice. */

static long ring_ensure(void) {
    if (g_ring == NULL) {
        astia_rings* r  = NULL;
        long         rc = astia_io_setup(RING_ENTRIES, 0, &r);
        if (rc < 0)
            return rc;
        if (r == NULL)
            return -EIO;
        g_ring = r;
    }
    return 0;
}

/* Settle an operation with an unknown fate: reap its completion, kicking
 * with to_submit=1 when the queue looks empty — resubmission is
 * idempotent (an entry the kernel already consumed is not consumed
 * again).  Returns 0 once settled; the *result* is discarded, because
 * the call that lost the race has already reported its failure to its
 * caller.  Returns < 0 only when the ring cannot be waited on at all —
 * then g_inflight stays set and the next transaction stops at this same
 * gate instead of compounding the unknown state. */
static long settle(void) {
    for (;;) {
        astia_cqe* c = astia_cq_peek(g_ring);
        long       rc;

        if (c != NULL) {
            int ours = (c->user_data == g_inflight);
            astia_cq_consume(g_ring, 1);
            if (ours) {
                g_inflight = 0;
                return 0;
            }
            continue; /* not the tag we owe — keep reaping */
        }
        rc = astia_io_enter(g_ring, 1, 1, ENTER_GETEVENTS, NULL);
        if (rc < 0)
            return rc;
    }
}

/* One operation, start to finish: the sync story of the async ring.
 * Exactly one SQE is in flight at a time, so the first completion we
 * find with our tag is ours and nothing else can be pending. */
static long transact(uint32_t op, int32_t fd, uint64_t off, const void* addr,
                     uint32_t len, uint32_t op_flags, uint32_t aux) {
    astia_rings* r;
    astia_sqe*   s;
    astia_cqe*   c;
    uint64_t     tag;
    long         rc;

    rc = ring_ensure();
    if (rc < 0)
        return rc;
    if (g_inflight != 0) { /* an earlier kick failed: settle it first */
        rc = settle();
        if (rc < 0)
            return rc;
    }

    r   = g_ring;
    tag = ++g_tag;

    s = astia_sqe_slot(r);
    if (s == NULL) {
        /* Full SQ means the kernel has not drained earlier entries —
           kick it and retry once.  Unreachable while operations are 1:1. */
        (void) astia_io_enter(r, 1, 0, 0, NULL);
        s = astia_sqe_slot(r);
        if (s == NULL)
            return -EBUSY;
    }

    memset(s, 0, sizeof *s);
    s->op        = op;
    s->fd        = fd;
    s->op_flags  = op_flags;
    s->offset    = off;
    s->address   = (uint64_t) (uintptr_t) addr;
    s->length    = len;
    s->aux       = aux;
    s->user_data = tag;
    astia_sq_publish(r, 1);

    /* kick + wait in a single syscall; the reaping below costs none */
    rc = astia_io_enter(r, 1, 1, ENTER_GETEVENTS, NULL);
    if (rc < 0) {
        /* The kick failed after publish: the SQE may or may not be in
           flight.  One retry — resubmitting an already-consumed entry
           is a no-op — and if that fails too, record the tag and stop:
           the next transaction settles it before touching the queue,
           so no operation is ever applied twice and no waiter can hang
           on a completion that will never come. */
        rc = astia_io_enter(r, 1, 1, ENTER_GETEVENTS, NULL);
        if (rc < 0) {
            g_inflight = tag;
            return rc;
        }
    }

    for (;;) {
        c = astia_cq_peek(r);
        if (c == NULL) {
            /* The completion we waited for must be there; re-wait in
               case a foreign entry consumed our slot in the count. */
            rc = astia_io_enter(r, 0, 1, ENTER_GETEVENTS, NULL);
            if (rc < 0)
                return rc;
            continue;
        }
        if (c->user_data == tag) {
            rc = c->result;
            astia_cq_consume(r, 1);
            return rc;
        }
        /* Defensive: with one operation in flight no foreign completion
           can be pending; drop it rather than wedge the stream. */
        astia_cq_consume(r, 1);
    }
}

long __astia_fio_open(const char* path, int flags, int mode) {
    uint32_t of;

    if ((flags & FIO_READ) && (flags & FIO_WRITE))
        of = O_RDWR;
    else if (flags & FIO_WRITE)
        of = O_WRONLY;
    else
        of = O_RDONLY;

    if (flags & FIO_CREAT)
        of |= O_CREAT;
    if (flags & FIO_EXCL)
        of |= O_EXCL;
    if (flags & FIO_TRUNC)
        of |= O_TRUNC;
    if (flags & FIO_APPEND)
        of |= O_APPEND;

    return transact(OP_OPENAT, AT_FDCWD, 0, path, 0, of, (uint32_t) mode);
}

long __astia_fio_read(int fd, uint64_t off, void* buf, size_t n) {
    return transact(OP_READ, fd, off, buf, (uint32_t) n, 0, 0);
}

long __astia_fio_write(int fd, uint64_t off, const void* buf, size_t n) {
    return transact(OP_WRITE, fd, off, buf, (uint32_t) n, 0, 0);
}

long __astia_fio_readv(int fd, uint64_t off, const struct iovec* iov, uint32_t count) {
    /* The layout assert above is what makes this cast sound; for
       READV/WRITEV the SQE's length is the entry *count*, and its
       address the list itself. */
    return transact(OP_READV, fd, off, iov, count, 0, 0);
}

long __astia_fio_writev(int fd, uint64_t off, const struct iovec* iov, uint32_t count) {
    return transact(OP_WRITEV, fd, off, iov, count, 0, 0);
}

long __astia_fio_close(int fd) {
    return transact(OP_CLOSE, fd, 0, NULL, 0, 0, 0);
}

long __astia_fio_size(int fd, uint64_t* out) {
    astia_stat st;
    long       rc;

    /* fstat convention pinned by the STATX row's footnote in
       <astia/api/aio.h>: address NULL -> fd is the stat target,
       offset the stat* out-parameter. */
    memset(&st, 0, sizeof st);
    rc = transact(OP_STATX, fd, (uint64_t) (uintptr_t) &st, NULL, (uint32_t) sizeof st, 0,
                  0);
    if (rc < 0)
        return rc;
    *out = st.size;
    return 0;
}
