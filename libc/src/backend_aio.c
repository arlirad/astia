#include "file_port.h"
#include "process_port.h"
#include <astia/api/aio.h>

#include <fcntl.h>
#include <string.h>

/*
 * astia libc — the file port over the async I/O ring.
 *
 * The ABI has no synchronous read/write calls: every blocking primitive
 * is an SQE on an I/O ring.  Synchronous behavior is therefore built
 * here, one operation at a time:
 *
 *   fill SQE -> publish -> one astia_io_submit() kick -> reap the CQE by
 *   its user_data tag -> return
 *   cqe.result unchanged (negative = -errno: the port speaks the ABI's
 *   convention verbatim, file.c folds it into errno).
 *
 * The ring is set up lazily on the first file call in each thread. Signal
 * handlers use a separate thread-local ring while an interrupted operation
 * is still in flight.
 *
 * This translation unit only ever *executes* on astia (astia_raw6 is a
 * real trap there).  Host test binaries link tests/backend_host.c
 * instead; the file still compiles everywhere so the ABI never rots
 * silently — that compile is its check until a kernel-side ring exists
 * to run against.
 */

enum { RING_ENTRIES = 64 };

/* Translate libc's native open word to the AIO OPENAT wire word.  Keeping
 * this table explicit prevents native values (notably O_CREAT/O_TRUNC and
 * O_DIRECTORY) from leaking into the ABI. */
static uint32_t aio_open_flags_from_native(int flags) {
    uint32_t wire = (flags & O_RDWR) == O_RDWR ? ASTIA_AIO_O_RDWR
                    : (flags & O_WRONLY)       ? ASTIA_AIO_O_WRONLY
                                               : ASTIA_AIO_O_RDONLY;
    if (flags & O_CREAT)
        wire |= ASTIA_AIO_O_CREAT;
    if (flags & O_EXCL)
        wire |= ASTIA_AIO_O_EXCL;
    if (flags & O_TRUNC)
        wire |= ASTIA_AIO_O_TRUNC;
    if (flags & O_APPEND)
        wire |= ASTIA_AIO_O_APPEND;
    if (flags & O_NONBLOCK)
        wire |= ASTIA_AIO_O_NONBLOCK;
    if (flags & O_CLOEXEC)
        wire |= ASTIA_AIO_O_CLOEXEC;
    if (flags & O_DIRECTORY)
        wire |= ASTIA_AIO_O_DIRECTORY;
    return wire;
}

ASTIA_STATIC_ASSERT(FIO_OFF_APPEND == OFF_APPEND,
                    "port append offset must equal ABI OFF_APPEND");
ASTIA_STATIC_ASSERT(ASTIA_AIO_O_CREAT == 0x04 && ASTIA_AIO_O_TRUNC == 0x10 &&
                        ASTIA_AIO_O_APPEND == 0x20 && ASTIA_AIO_O_NONBLOCK == 0x100,
                    "AIO open flag wire values changed");
ASTIA_STATIC_ASSERT(sizeof(struct iovec) == sizeof(astia_iovec) &&
                        offsetof(struct iovec, iov_base) == offsetof(astia_iovec, base) &&
                        offsetof(struct iovec, iov_len) == offsetof(astia_iovec, len),
                    "port iovec must be the ABI's astia_iovec");

static _Thread_local astia_rings* g_ring;
static _Thread_local uint64_t     g_tag;
static _Thread_local astia_rings* g_signal_ring;
static _Thread_local uint64_t     g_signal_tag;
static _Thread_local int          g_signal_transaction;
static _Thread_local uint64_t     g_inflight; /* 0: none.  Tag of an operation whose kick
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
        rc = astia_io_submit(g_ring, 1);
        if (rc < 0)
            return rc;
        (void) astia_yield();
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

    if (__astia_signal_handler_depth != 0 && !g_signal_transaction) {
        astia_rings* saved_ring = g_ring;
        uint64_t     saved_tag = g_tag;
        uint64_t     saved_inflight = g_inflight;
        g_signal_transaction = 1;
        g_ring = g_signal_ring;
        g_tag = g_signal_tag;
        g_inflight = 0;
        rc = transact(op, fd, off, addr, len, op_flags, aux);
        g_signal_ring = g_ring;
        g_signal_tag = g_tag;
        g_ring = saved_ring;
        g_tag = saved_tag;
        g_inflight = saved_inflight;
        g_signal_transaction = 0;
        return rc;
    }

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
        (void) astia_io_submit(r, 1);
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

    rc = astia_io_submit(r, 1);
    if (rc < 0) {
        /* The kick failed after publish: the SQE may or may not be in
           flight.  One retry — resubmitting an already-consumed entry
           is a no-op — and if that fails too, record the tag and stop:
           the next transaction settles it before touching the queue,
           so no operation is ever applied twice and no waiter can hang
           on a completion that will never come. */
        rc = astia_io_submit(r, 1);
        if (rc < 0) {
            g_inflight = tag;
            return rc;
        }
    }

    for (;;) {
        c = astia_cq_peek(r);
        if (c == NULL) {
            rc = astia_io_submit(r, 0);
            if (rc < 0)
                return rc;
            (void) astia_yield();
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

void __astia_fio_prepare_exec(void) {
    if (g_ring != NULL)
        (void) astia_io_destroy(g_ring);
    g_ring     = NULL;
    g_inflight = 0;
    g_tag      = 0;
    g_signal_ring = NULL;
    g_signal_tag = 0;
}

void __astia_fio_prepare_fork(void) {
    /* The ring mapping and its kernel slot are process-owned.  Do not carry
       either side of that ownership across fork: both continuations lazily
       acquire a private ring before their next file operation. */
    if (g_ring != NULL)
        (void) astia_io_destroy(g_ring);
    g_ring     = NULL;
    g_inflight = 0;
    g_tag      = 0;
    g_signal_ring = NULL;
    g_signal_tag = 0;
}

long __astia_fio_open(const char* path, int flags, int mode) {
    uint32_t of;

    if ((flags & FIO_READ) && (flags & FIO_WRITE))
        of = ASTIA_AIO_O_RDWR;
    else if (flags & FIO_WRITE)
        of = ASTIA_AIO_O_WRONLY;
    else
        of = ASTIA_AIO_O_RDONLY;

    if (flags & FIO_CREAT)
        of |= ASTIA_AIO_O_CREAT;
    if (flags & FIO_EXCL)
        of |= ASTIA_AIO_O_EXCL;
    if (flags & FIO_TRUNC)
        of |= ASTIA_AIO_O_TRUNC;
    if (flags & FIO_APPEND)
        of |= ASTIA_AIO_O_APPEND;
    if (flags & FIO_NONBLOCK)
        of |= ASTIA_AIO_O_NONBLOCK;
    if (flags & FIO_CLOEXEC)
        of |= ASTIA_AIO_O_CLOEXEC;

    return transact(OP_OPENAT, AT_FDCWD, 0, path, 0, of, (uint32_t) mode);
}

long __astia_fio_openat(int dirfd, const char* path, int flags, int mode) {
    uint32_t of = aio_open_flags_from_native(flags);
    return transact(OP_OPENAT, dirfd, 0, path, 0, of, (uint32_t) mode);
}

long __astia_fio_read(int fd, uint64_t off, void* buf, size_t n) {
    if (n > UINT32_MAX)
        return -EINVAL;
    return transact(OP_READ, fd, off, buf, (uint32_t) n, 0, 0);
}

long __astia_fio_read_batch(struct astia_fio_batch_read* requests, size_t count) {
    astia_rings* r;
    size_t       submitted = 0;
    size_t       completed = 0;

    if (count == 0)
        return 0;
    if (count > RING_ENTRIES)
        return -EINVAL;
    if (requests == NULL)
        return -EFAULT;
    for (size_t i = 0; i < count; ++i)
        if (requests[i].length > UINT32_MAX)
            return -EINVAL;
    if (ring_ensure() < 0)
        return -EIO;
    r = g_ring;
    if (g_inflight != 0) {
        long rc = settle();
        if (rc < 0)
            return rc;
    }

    while (submitted < count) {
        astia_sqe* s = astia_sqe_slot(r);
        if (s == NULL)
            return -EBUSY;
        memset(s, 0, sizeof *s);
        s->op                      = OP_READ;
        s->fd                      = requests[submitted].fd;
        s->offset                  = requests[submitted].offset;
        s->address                 = (uint64_t) (uintptr_t) requests[submitted].buffer;
        s->length                  = (uint32_t) requests[submitted].length;
        s->user_data               = ++g_tag;
        requests[submitted].result = -EAGAIN;
        ++submitted;
        astia_sq_publish(r, 1);
    }

    long rc = astia_io_submit(r, (uint32_t) count);
    if (rc < 0)
        return rc;
    while (completed < count) {
        astia_cqe* c = astia_cq_peek(r);
        if (c == NULL) {
            rc = astia_io_submit(r, 0);
            if (rc < 0)
                return rc;
            (void) astia_yield();
            continue;
        }
        uint64_t tag = c->user_data;
        /* Tags are allocated in submission order; the batch has no other
           producer, so map the completion by its monotonic tag. */
        uint64_t first = g_tag - count + 1;
        if (tag >= first && tag <= g_tag)
            requests[tag - first].result = c->result;
        astia_cq_consume(r, 1);
        ++completed;
    }
    return 0;
}

long __astia_fio_write(int fd, uint64_t off, const void* buf, size_t n) {
    if (n > UINT32_MAX)
        return -EINVAL;
    return transact(OP_WRITE, fd, off, buf, (uint32_t) n, 0, 0);
}

long __astia_fio_ftruncate(int fd, uint64_t length) {
    return transact(OP_FTRUNCATE, fd, length, NULL, 0, 0, 0);
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

long __astia_fio_stat(const char* path, void* out, uint32_t length) {
    return transact(OP_STATX, -1, (uint64_t) (uintptr_t) out, path, length, 0, 0);
}
long __astia_fio_statat(int dirfd, const char* path, void* out, uint32_t length,
                        int flags) {
    return transact(OP_STATX, dirfd, (uint64_t) (uintptr_t) out, path, length,
                    flags & 0x100 ? STATX_NOFOLLOW : 0, 0);
}
long __astia_fio_lstat(const char* path, void* out, uint32_t length) {
    return transact(OP_STATX, -1, (uint64_t) (uintptr_t) out, path, length,
                    STATX_NOFOLLOW, 0);
}

long __astia_fio_fstat(int fd, void* out, uint32_t length) {
    return transact(OP_STATX, fd, (uint64_t) (uintptr_t) out, NULL, length, 0, 0);
}

long __astia_fio_mkdir(const char* path, int mode) {
    return transact(OP_MKDIRAT, -1, 0, path, 0, 0, (uint32_t) mode);
}

long __astia_fio_chmod(const char* path, int mode) {
    return transact(OP_CHMOD, -1, 0, path, 0, 0, (uint32_t) mode);
}
long __astia_fio_chown(const char* path, unsigned int uid, unsigned int gid,
                       int nofollow) {
    return transact(OP_CHOWN, -1, gid, path, 0, nofollow ? STATX_NOFOLLOW : 0, uid);
}
long __astia_fio_fchown(int fd, unsigned int uid, unsigned int gid) {
    return transact(OP_CHOWN, fd, gid, NULL, 0, 0, uid);
}

long __astia_fio_utime(const char* path) {
    return transact(OP_UTIMENSAT, AT_FDCWD, 0, path, 0, 0, 0);
}

long __astia_fio_unlink(const char* path, int directory) {
    return transact(OP_UNLINKAT, -1, 0, path, 0, directory ? UNLINKAT_REMOVEDIR : 0, 0);
}
long __astia_fio_unlinkat(int dirfd, const char* path, int directory) {
    return transact(OP_UNLINKAT, dirfd, 0, path, 0, directory ? UNLINKAT_REMOVEDIR : 0,
                    0);
}

long __astia_fio_rename(const char* old_path, const char* new_path) {
    return transact(OP_RENAMEAT, -1, (uint64_t) (uintptr_t) new_path, old_path, 0, 0, 0);
}
long __astia_fio_renameat(int olddirfd, const char* old_path, int newdirfd,
                          const char* new_path) {
    return transact(OP_RENAMEAT, olddirfd, (uint64_t) (uintptr_t) new_path, old_path,
                    (uint32_t) newdirfd, 0, 0);
}
long __astia_fio_link(const char* old_path, const char* new_path) {
    return transact(OP_LINKAT, -1, (uint64_t) (uintptr_t) new_path, old_path, 0, 0, 0);
}
long __astia_fio_linkat(int olddirfd, const char* old_path, int newdirfd,
                        const char* new_path) {
    return transact(OP_LINKAT, olddirfd, (uint64_t) (uintptr_t) new_path, old_path,
                    (uint32_t) newdirfd, 0, 0);
}
long __astia_fio_mkdirat(int dirfd, const char* path, int mode) {
    return transact(OP_MKDIRAT, dirfd, 0, path, 0, 0, (uint32_t) mode);
}
long __astia_fio_symlink(const char* target, const char* path) {
    size_t length = strlen(target);
    if (length > UINT32_MAX)
        return -EINVAL;
    return transact(OP_SYMLINKAT, -1, (uint64_t) (uintptr_t) path, target,
                    (uint32_t) length, 0, 0);
}
long __astia_fio_readlink(const char* path, char* buffer, size_t capacity) {
    if (capacity > UINT32_MAX)
        return -EINVAL;
    return transact(OP_READLINKAT, -1, (uint64_t) (uintptr_t) buffer, path,
                    (uint32_t) capacity, 0, 0);
}
long __astia_fio_readlinkat(int dirfd, const char* path, char* buffer, size_t capacity) {
    if (capacity > UINT32_MAX)
        return -EINVAL;
    return transact(OP_READLINKAT, dirfd, (uint64_t) (uintptr_t) buffer, path,
                    (uint32_t) capacity, 0, 0);
}

long __astia_fio_readdir(const char* path, uint64_t index, astia_dirent* out) {
    return transact(OP_READDIR, (int32_t) index, (uint64_t) (uintptr_t) path, out,
                    (uint32_t) sizeof *out, 0, 0);
}

long __astia_fio_ioctl(int fd, uint64_t request, void* arg, size_t length) {
    if (length > UINT32_MAX)
        return -EINVAL;
    return transact(OP_IOCTL, fd, request, arg, (uint32_t) length, 0, 0);
}

long __astia_fio_pipe(int fds[2]) {
    return transact(OP_PIPE, -1, 0, fds, 2u * sizeof(int), 0, 0);
}

long __astia_fio_socket(int domain, int type, int protocol) {
    return transact(OP_SOCKET, domain, 0, NULL, 0, (uint32_t) type, (uint32_t) protocol);
}
long __astia_fio_socketpair(int domain, int type, int protocol, int fds[2]) {
    return transact(OP_SOCKETPAIR, domain, 0, fds, 2 * sizeof(int), (uint32_t) type,
                    (uint32_t) protocol);
}
long __astia_fio_shutdown(int fd, int how) {
    return transact(OP_SHUTDOWN, fd, 0, NULL, 0, (uint32_t) how, 0);
}
long __astia_fio_bind(int fd, const void* address, uint32_t length) {
    return transact(OP_BIND, fd, 0, address, length, 0, 0);
}
long __astia_fio_listen(int fd, int backlog) {
    return transact(OP_LISTEN, fd, 0, NULL, 0, 0, (uint32_t) backlog);
}
long __astia_fio_accept(int fd, void* address, uint32_t* length) {
    (void) address;
    (void) length;
    return transact(OP_ACCEPT, fd, 0, NULL, 0, 0, 0);
}
long __astia_fio_connect(int fd, const void* address, uint32_t length) {
    return transact(OP_CONNECT, fd, 0, address, length, 0, 0);
}
long __astia_fio_send(int fd, const void* buf, size_t n, int flags) {
    (void) flags;
    if (n > UINT32_MAX)
        return -EINVAL;
    return transact(OP_SEND, fd, OFF_APPEND, buf, (uint32_t) n, 0, 0);
}
long __astia_fio_recv(int fd, void* buf, size_t n, int flags) {
    (void) flags;
    if (n > UINT32_MAX)
        return -EINVAL;
    return transact(OP_RECV, fd, OFF_APPEND, buf, (uint32_t) n, 0, 0);
}
long __astia_fio_sendto(int fd, const void* buf, size_t n, int flags, const void* addr,
                        uint32_t alen) {
    (void) flags;
    if (n > UINT32_MAX || alen != sizeof(astia_sockaddr))
        return -EINVAL;
    return transact(OP_SENDTO, fd, (uint64_t) (uintptr_t) addr, buf, (uint32_t) n, 0, 0);
}
long __astia_fio_recvfrom(int fd, void* buf, size_t n, int flags, void* addr,
                          uint32_t* alen) {
    (void) flags;
    if (n > UINT32_MAX || !addr || !alen)
        return -EINVAL;
    return transact(OP_RECVFROM, fd, (uint64_t) (uintptr_t) addr, buf, (uint32_t) n, 0,
                    0);
}

long __astia_fio_dup(int fd) {
    return transact(OP_DUP, fd, 0, NULL, 0, 0, 0);
}

long __astia_fio_dup2(int old_fd, int new_fd) {
    return transact(OP_DUP2, old_fd, (uint64_t) (uint32_t) new_fd, NULL, 0, 0, 0);
}

long __astia_fio_fcntl(int fd, int command, unsigned int argument) {
    return transact(OP_FCNTL, fd, 0, NULL, 0, (uint32_t) command, argument);
}

long __astia_fio_sleep(uint64_t nanoseconds, uint64_t* remaining_nanoseconds) {
    return transact(OP_SLEEP, -1, nanoseconds, remaining_nanoseconds,
                    remaining_nanoseconds == NULL ? 0 : sizeof *remaining_nanoseconds,
                    0, 0);
}

long __astia_fio_poll(struct pollfd* fds, size_t count, int timeout_ms) {
    astia_rings*  r;
    uint64_t      tags[IO_MAX_ENTRIES];
    uint64_t      cancel_tags[IO_MAX_ENTRIES];
    unsigned char pending[IO_MAX_ENTRIES] = {0};
    uint64_t      sleep_tag = 0;
    size_t        submitted = 0, pending_count = 0, ready_count = 0;
    size_t        cancel_count = 0, cancel_submitted = 0, cancel_acks = 0;
    long          rc = 0;
    long          wait_error = 0;
    int           sleep_pending = 0;
    int           stop = 0;

    if (count > RING_ENTRIES - (timeout_ms >= 0 ? 1u : 0u) ||
        (count != 0 && fds == NULL) || timeout_ms < -1)
        return -EINVAL;
    for (size_t i = 0; i < count; ++i)
        fds[i].revents = 0;
    if (count == 0) {
        if (timeout_ms == 0)
            return 0;
        return __astia_fio_sleep(timeout_ms < 0
                                     ? UINT64_MAX
                                     : (uint64_t) (unsigned) timeout_ms * 1000000ull,
                                 NULL);
    }
    rc = ring_ensure();
    if (rc < 0)
        return rc;
    if (g_inflight != 0) {
        rc = settle();
        if (rc < 0)
            return rc;
    }
    r = g_ring;
    for (size_t i = 0; i < count; ++i) {
        astia_sqe* s = astia_sqe_slot(r);
        if (s == NULL) {
            rc = -EBUSY;
            goto cancel;
        }
        tags[i]    = ++g_tag;
        pending[i] = 1;
        ++pending_count;
        memset(s, 0, sizeof *s);
        s->op        = OP_POLL_ADD;
        s->fd        = fds[i].fd;
        s->aux       = (uint32_t) (unsigned short) fds[i].events;
        s->user_data = tags[i];
        astia_sq_publish(r, 1);
        ++submitted;
    }
    if (timeout_ms >= 0) {
        astia_sqe* s = astia_sqe_slot(r);
        if (s == NULL) {
            rc = -EBUSY;
            goto cancel;
        }
        sleep_tag     = ++g_tag;
        sleep_pending = 1;
        ++pending_count;
        memset(s, 0, sizeof *s);
        s->op        = OP_SLEEP;
        s->offset    = (uint64_t) (unsigned) timeout_ms * 1000000ull;
        s->user_data = sleep_tag;
        astia_sq_publish(r, 1);
        ++submitted;
    }
    rc = astia_io_submit(r, (uint32_t) submitted);
    if (rc < 0)
        goto cancel;
    if ((size_t) rc != submitted) {
        rc = -EBUSY;
        goto cancel;
    }
    while (!stop && pending_count != 0) {
        astia_cqe* c;
        while (!stop && (c = astia_cq_peek(r)) != NULL) {
            for (size_t i = 0; i < count; ++i) {
                if (pending[i] && tags[i] == c->user_data) {
                    pending[i] = 0;
                    --pending_count;
                    if (c->result < 0) {
                        if (wait_error == 0)
                            wait_error = c->result;
                    }
                    else {
                        fds[i].revents = (short) c->result;
                        if (c->result != 0) {
                            ++ready_count;
                            stop = 1;
                        }
                    }
                    break;
                }
            }
            if (sleep_pending && sleep_tag == c->user_data) {
                sleep_pending = 0;
                --pending_count;
                if (c->result < 0 && wait_error == 0)
                    wait_error = c->result;
                stop = 1;
            }
            astia_cq_consume(r, 1);
        }
        if (!stop) {
            rc = astia_io_submit(r, 0);
            if (rc < 0)
                goto cancel;
            (void) astia_yield();
        }
    }

cancel:
    for (size_t i = 0; i < count; ++i) {
        if (!pending[i])
            continue;
        for (;;) {
            astia_sqe* s = astia_sqe_slot(r);
            if (s != NULL) {
                memset(s, 0, sizeof *s);
                s->op        = OP_CANCEL;
                s->offset    = tags[i];
                s->user_data = ++g_tag;
                cancel_tags[cancel_count++] = s->user_data;
                astia_sq_publish(r, 1);
                break;
            }
            rc = astia_io_submit(r, 0);
            if (rc < 0)
                goto cancel_wait;
            (void) astia_yield();
        }
    }
    if (sleep_pending) {
        for (;;) {
            astia_sqe* s = astia_sqe_slot(r);
            if (s != NULL) {
                memset(s, 0, sizeof *s);
                s->op        = OP_CANCEL;
                s->offset    = sleep_tag;
                s->user_data = ++g_tag;
                cancel_tags[cancel_count++] = s->user_data;
                astia_sq_publish(r, 1);
                break;
            }
            rc = astia_io_submit(r, 0);
            if (rc < 0)
                goto cancel_wait;
            (void) astia_yield();
        }
    }
cancel_wait:
    while (cancel_submitted < cancel_count || pending_count != 0 ||
           cancel_acks < cancel_count) {
        if (cancel_submitted < cancel_count) {
            rc = astia_io_submit(r, (uint32_t) (cancel_count - cancel_submitted));
            if (rc < 0)
                break;
            cancel_submitted += (size_t) rc;
        }
        astia_cqe* c;
        while ((c = astia_cq_peek(r)) != NULL) {
            for (size_t i = 0; i < count; ++i) {
                if (pending[i] && tags[i] == c->user_data) {
                    pending[i] = 0;
                    --pending_count;
                    if (c->result >= 0 && c->result != 0) {
                        fds[i].revents = (short) c->result;
                        ++ready_count;
                    }
                    else if (c->result < 0 && wait_error == 0)
                        wait_error = c->result;
                    break;
                }
            }
            if (sleep_pending && sleep_tag == c->user_data) {
                sleep_pending = 0;
                --pending_count;
                if (c->result < 0 && wait_error == 0)
                    wait_error = c->result;
            }
            for (size_t i = 0; i < cancel_count; ++i)
                if (cancel_tags[i] == c->user_data) {
                    ++cancel_acks;
                    break;
                }
            astia_cq_consume(r, 1);
        }
        if (cancel_submitted == cancel_count && pending_count == 0 &&
            cancel_acks == cancel_count)
            break;
        (void) astia_io_submit(r, 0);
        (void) astia_yield();
    }
    if (rc < 0 && ready_count == 0)
        return rc;
    if (wait_error < 0 && ready_count == 0)
        return wait_error;
    return (long) ready_count;
}

long __astia_fio_eventfd(unsigned int initial_value, int flags) {
    if ((flags & ~(ASTIA_EFD_NONBLOCK | ASTIA_EFD_CLOEXEC)) != 0)
        return -EINVAL;
    return transact(OP_EVENTFD_CREATE, -1, (uint64_t) initial_value, NULL, 0,
                    (uint32_t) flags, 0);
}

void __astia_fio_after_fork(void) {
    /* The ring's user alias belongs to the parent address space.  The child
       inherits libc data through CoW, so it must acquire its own ring before
       issuing any I/O. */
    g_ring     = NULL;
    g_inflight = 0;
    g_tag      = 0;
}
