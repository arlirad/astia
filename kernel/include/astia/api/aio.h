#pragma once

/*
 * astia/api/aio.h — asynchronous I/O: the submission/completion ring ABI.
 *
 * I/O contract:
 *   Every operation that can block (read, write, open, close, stat, poll,
 *   wait, spawn, sleep, networking) is an OP_* executed on an I/O ring.
 *   There are no synchronous read/write syscalls — by construction.
 *   astia_futex() in <astia/api/syscalls.h> is the one synchronization
 *   primitive outside the rings, and its uncontended path never sleeps.
 *
 * Performance model:
 *   - One shared mapping holds the submission (SQ) and completion (CQ)
 *     rings plus the SQE/CQE arrays.  Submitting and reaping are plain
 *     memory operations with acquire/release ordering; reaping costs zero
 *     syscalls.
 *   - One astia_io_submit() kick submits a whole batch and returns
 *     immediately.  Completions are read in batches straight out of the CQ;
 *     SIGAIO notifies the owning process when CQEs become available.
 *   - Kernel and userspace own separate cache lines for every cursor:
 *     no false sharing on the hot path.
 *   - SQEs are consumed in place: no per-SQE copy-in, no index array.
 *   - astia_io_register() pins files/buffers once, removing per-op fd-table
 *     lookups and pin/unpin work (SQF_FIXED_FILE / SQF_FIXED_BUF).
 *   - sqe.user_data echoes through the CQE, so userspace routes completions
 *     without a side table.
 *   - Buffers are accessed in place after pinning: zero-copy I/O.
 *
 * Return convention:
 *   cqe.result >= 0 on success (payload: bytes, fd, pid, revents), or a
 *   negative value from <astia/api/errno.h>.
 *
 * Typical flow:
 *
 *   astia_rings* r;
 *   astia_io_setup(256, 0, &r);                    // 1 syscall
 *
 *   astia_sqe* s = astia_sqe_slot(r);              // userspace
 *   s->op        = OP_WRITE;
 *   s->fd        = STDOUT;
 *   s->address   = (uint64_t) (uintptr_t) buf;
 *   s->length    = n;
 *   s->user_data = 42;
 *   astia_sq_publish(r, 1);
 *   astia_io_submit(r, 1);                         // kick: 1 syscall / batch
 *
 *   astia_cqe* c;
 *   while ((c = astia_cq_peek(r))) {               // reap: 0 syscalls
 *       handle(c->user_data, c->result);
 *       astia_cq_consume(r, 1);
 *   }
 * Sits on <astia/api/syscalls.h> (SYS_* numbers, astia_raw6 trap).
 */

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* version and capacities                                              */
/* ------------------------------------------------------------------ */

/* ABI ring capacities: power of two, 1 .. IO_MAX_ENTRIES.  The kernel rounds
   the request up; the actual sizes land in astia_rings.  The CQ is sized
   to max(64, 2 * sq_entries) so completions never lag submissions. */
enum { IO_MAX_ENTRIES = 4096 };

/* The current implementation has fixed, non-allocating kernel storage.  These
   values are a read-only capacity contract: exceeding the ring pool returns
   -ENFILE, and exhausting one ring's parked-operation queue posts -EBUSY. */
enum {
    ASTIA_AIO_MAX_RING_SLOTS        = 16,
    ASTIA_AIO_MAX_RING_ENTRIES      = 64,
    ASTIA_AIO_MAX_PARKED_OPERATIONS = 64,
    ASTIA_AIO_MAX_OPERATION_BYTES   = 0x7ffff000,
};

/* ------------------------------------------------------------------ */
/* ring ABI                                                            */
/*                                                                     */
/* Mapping layout (one contiguous region, offsets filled by the kernel):*/
/*                                                                     */
/*   +0                astia_rings       (header, 64 B)                */
/*   +sq_off           astia_sq_ring     (128 B, cursors split by line)*/
/*   +cq_off           astia_cq_ring     (128 B, cursors split by line)*/
/*   +sqes_off         astia_sqe[sq_entries]                           */
/*   +cqes_off         astia_cqe[cq_entries]                           */
/* ------------------------------------------------------------------ */

#define RINGS_MAGIC 0x30525341u /* "ASR0" in little-endian */

typedef struct astia_rings {
    uint32_t magic;
    uint32_t version;  /* API_VERSION */
    uint64_t features; /* FEAT_* set by the kernel */
    uint32_t sq_entries;
    uint32_t cq_entries;
    uint32_t sq_off;
    uint32_t cq_off;
    uint32_t sqes_off;
    uint32_t cqes_off;
    uint32_t flags; /* RINGF_* sticky state */
    uint32_t resv0;
    uint64_t resv1[2];
} astia_rings;

typedef struct astia_sq_ring {
    /* line 0: kernel-written, userspace-read */
    uint32_t head __attribute__((aligned(64)));
    uint32_t flags;   /* RINGF_NEED_WAKEUP (kernel -> user) */
    uint32_t dropped; /* diagnostic; expected to stay 0 */
    /* line 1: userspace-written, kernel-read */
    uint32_t tail __attribute__((aligned(64)));
    uint32_t mask; /* entries - 1 */
    uint32_t entries;
    uint32_t resv[2];
} astia_sq_ring;

typedef struct astia_cq_ring {
    /* line 0: userspace-written (reap cursor) */
    uint32_t head __attribute__((aligned(64)));
    uint32_t resv0;
    /* line 1: kernel-written, userspace-read */
    uint32_t tail __attribute__((aligned(64)));
    uint32_t overflow; /* reserved; the kernel never drops completions */
    uint32_t mask;
    uint32_t entries;
    uint32_t notify_generation; /* kernel -> user SIGAIO generation */
    uint32_t notify_ack;        /* user -> kernel acknowledged generation */
} astia_cq_ring;

/*
 * Submission queue entry — 48 bytes, consumed in place by the kernel.
 *
 * Per-op field usage:
 *
 *   op            fd          offset             address            length        aux
 *   READ/WRITE    handle      file offset        buffer             byte count    buf
 * index* (OFF_APPEND = append)        (ignored if FIXED_BUF)           (FIXED_BUF)
 *   READV/WRITEV  handle      file offset        iovec[]            iov count     -
 *   OPENAT        dirfd       -                  pathname           -             mode
 *   CLOSE         handle      -                  -                  -             -
 *   STATX ‡       dirfd       stat* (out)        pathname           sizeof(stat) reserved
 *   FSYNC         handle      -                  -                  -             -
 *   UNLINKAT      dirfd       -                  pathname           -             -
 *   MKDIRAT       dirfd       -                  pathname           -             mode
 *   RENAMEAT      olddirfd    newpath*           oldpath            -             -
 *   DUP           handle      -                  -                  -             -
 *   DUP2          handle      new handle         -                  -             -
 *   SLEEP         -           ns (or deadline†)  -                  -             -
 *   WAIT          pid (-1 any)-                  int32* status(out) -             -
 *   POLL_ADD      handle      -                  -                  -             events
 *   SPAWN         -           -                  astia_spawn*       sizeof(spawn) -
 *   CANCEL        -           user_data target   -                  -             -
 *   ACCEPT        sock        -                  astia_sockaddr*    -             -
 *   CONNECT       sock        -                  astia_sockaddr*    -             -
 *   SEND/RECV     sock        -                  buffer             byte count    -
 *   SENDTO        sock        sockaddr*         buffer             byte count    -
 *   RECVFROM      sock        sockaddr*         buffer             byte count    -
 *   UTIMENSAT     dirfd       times[2]*§        pathname           -             flags
 *
 *   * FIXED_BUF: address ignored, aux selects an astia_reg_buf slot.
 *   † default relative; SLEEP_ABSOLUTE makes offset a monotonic absolute
 *     deadline in nanoseconds.
 *   ‡ STATX: address NULL -> fd is the stat target (fstat); address a
 *     pathname -> fd is the dirfd.
 *   § UTIMENSAT: offset points at astia_timespec[2]; NULL means both times
 *     are UTIME_NOW.  flags accepts AT_SYMLINK_NOFOLLOW.
 *
 * cqe.result payloads: READ/WRITE/SEND/RECV -> bytes; OPENAT/ACCEPT -> fd;
 * SPAWN/WAIT -> pid; POLL_ADD -> revents; otherwise 0 or -errno.
 */
typedef struct astia_sqe {
    uint32_t op;        /* OP_* */
    uint32_t flags;     /* SQF_* */
    int32_t  fd;        /* primary handle: fd, dirfd, pid, socket */
    uint32_t op_flags;  /* per-op flag word: O_*, RWF_*, POLL*, W*, ... */
    uint64_t offset;    /* file offset | second pointer | deadline ns */
    uint64_t address;   /* buffer | iovec[] | pathname | out-struct */
    uint32_t length;    /* byte count | element count | struct size */
    uint32_t aux;       /* mode | fixed-buffer index | clock id */
    uint64_t user_data; /* opaque tag, echoed verbatim into the CQE */
} astia_sqe;

typedef struct astia_cqe {
    uint64_t user_data; /* echo of the producing SQE */
    int32_t  result;    /* >= 0 payload, < 0 = -errno (clamped to 2 GiB) */
    uint32_t flags;     /* CQF_* */
} astia_cqe;

/* supporting payload types */

typedef struct astia_iovec {
    void*    base;
    uint64_t len;
} astia_iovec;

typedef struct astia_stat {
    uint64_t dev;
    uint64_t ino;
    uint64_t size;
    uint64_t blocks;
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    /* Wall-clock seconds/nanoseconds.  A filesystem that records no
       timestamp reports zero; the kernel never substitutes a synthetic
       epoch. */
    uint64_t atime_sec;
    uint64_t atime_nsec;
    uint64_t mtime_sec;
    uint64_t mtime_nsec;
    uint64_t ctime_sec;
    uint64_t ctime_nsec;
    uint64_t resv[4];
} astia_stat;

typedef struct astia_dirent {
    uint32_t type;
    uint32_t namelen;
    char     name[256];
    uint64_t ino; /* filesystem inode; 0 when the mount exposes none */
} astia_dirent;

typedef struct astia_spawn {
    const char*        path;
    const char* const* argv;
    const char* const* envp;
    uint64_t           flags; /* SPAWN_* */
    uint64_t           resv;
} astia_spawn;

typedef struct astia_sockaddr {
    uint32_t len;    /* in: capacity, out: actual length */
    uint32_t family; /* out: AF_* */
    uint8_t  data[120];
} astia_sockaddr;

typedef struct astia_cmsghdr {
    uint64_t cmsg_len;
    int32_t  cmsg_level;
    int32_t  cmsg_type;
} astia_cmsghdr;

typedef struct astia_msghdr {
    void*    control;
    uint32_t controllen;
    uint32_t flags;
    uint32_t msg_flags;
    uint32_t resv;
} astia_msghdr;

typedef struct astia_reg_buf {
    uint64_t addr;
    uint32_t len;
    uint32_t resv;
} astia_reg_buf;

ASTIA_STATIC_ASSERT(sizeof(astia_timespec) == 16, "timespec ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_dirent) == 272, "dirent ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_iovec) == 16, "iovec ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_stat) == 128, "stat ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_spawn) == 40, "spawn ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_sockaddr) == 128, "sockaddr ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_cmsghdr) == 16, "cmsghdr ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_msghdr) == 24, "msghdr ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_reg_buf) == 16, "reg_buf ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_rings) == 64, "rings header ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_sq_ring) == 128, "sq ring ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_cq_ring) == 128, "cq ring ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_sqe) == 48, "sqe ABI");
ASTIA_STATIC_ASSERT(sizeof(astia_cqe) == 16, "cqe ABI");
ASTIA_STATIC_ASSERT(offsetof(astia_sq_ring, head) == 0, "sq head line");
ASTIA_STATIC_ASSERT(offsetof(astia_sq_ring, tail) == 64, "sq tail line");
ASTIA_STATIC_ASSERT(offsetof(astia_cq_ring, head) == 0, "cq head line");
ASTIA_STATIC_ASSERT(offsetof(astia_cq_ring, tail) == 64, "cq tail line");

/* ------------------------------------------------------------------ */
/* opcodes — append-only after the OP_SPAWN removal; this tree owns      */
/* the ABI, so the former spawn slot was freed and later IDs moved down. */
/* ------------------------------------------------------------------ */

enum {
    OP_NOP      = 0,
    OP_READ     = 1,
    OP_WRITE    = 2,
    OP_READV    = 3,
    OP_WRITEV   = 4,
    OP_OPENAT   = 5,
    OP_CLOSE    = 6,
    OP_STATX    = 7,
    OP_FSYNC    = 8,
    OP_UNLINKAT = 9,
    OP_MKDIRAT  = 10,
    OP_RENAMEAT = 11,
    OP_SLEEP    = 12,
    OP_WAIT     = 13,
    OP_POLL_ADD = 14,
    OP_CANCEL   = 15,
    /* networking — numbers reserved now so growth never shifts them */
    OP_ACCEPT  = 16,
    OP_CONNECT = 17,
    OP_SEND    = 18,
    OP_RECV    = 19,
    OP_DUP     = 20,
    OP_DUP2    = 21,
    OP_READDIR = 22,
    /* Fence: dispatch waits for all older parked operations before applying
       the control request; later SQEs are ordered after its completion. */
    OP_IOCTL = 23,
    OP_PIPE  = 24,
    OP_CHMOD = 25,
    /* Set pathname atime/mtime from offset's astia_timespec[2] (NULL: now). */
    OP_UTIMENSAT      = 26,
    OP_SYMLINKAT      = 27,
    OP_READLINKAT     = 28,
    OP_CHOWN          = 29,
    OP_SOCKET         = 30,
    OP_BIND           = 31,
    OP_LISTEN         = 32,
    OP_SOCKETPAIR     = 33,
    OP_SHUTDOWN       = 34,
    OP_FCNTL          = 35,
    OP_SENDTO         = 36,
    OP_RECVFROM       = 37,
    OP_EVENTFD_CREATE = 38,
    OP_LINKAT         = 39,
    OP_FTRUNCATE      = 40,
    OP_GETSOCKOPT     = 41,
    OP_SENDMSG        = 42,
    OP_RECVMSG        = 43,
};

/* OP_EVENTFD_CREATE flags.  The initial counter is in SQE::offset. */
enum {
    ASTIA_EFD_NONBLOCK = 1u << 0,
    ASTIA_EFD_CLOEXEC  = 1u << 1,
};

/* OP_FCNTL commands.  The argument is in SQE::aux. */
enum {
    ASTIA_F_DUPFD         = 0,
    ASTIA_F_GETFD         = 1,
    ASTIA_F_SETFD         = 2,
    ASTIA_F_GETFL         = 3,
    ASTIA_F_SETFL         = 4,
    ASTIA_F_DUPFD_CLOEXEC = 1030,
};

enum {
    ASTIA_FD_CLOEXEC = 1,
    ASTIA_O_APPEND   = 0x0400,
    ASTIA_O_NONBLOCK = 0x0800,
};

/* OP_SENDMSG / OP_RECVMSG: BSD/Linux ancillary-data wire values. */
enum {
    ASTIA_SCM_RIGHTS       = 1,
    ASTIA_SCM_CREDENTIALS  = 2,
    ASTIA_MSG_CTRUNC       = 0x08,
    ASTIA_MSG_DONTWAIT     = 0x40,
    ASTIA_MSG_EOR          = 0x80,
    ASTIA_MSG_CMSG_CLOEXEC = 0x40000000,
};

/* OP_SOCKETPAIR op_flags / OP_SHUTDOWN op_flags. */
enum {
    SOCK_STREAM    = 1,
    SOCK_DGRAM     = 2,
    SOCK_RAW       = 3,
    SOCK_SEQPACKET = 5,
    SOL_SOCKET     = 1,
    SO_TYPE        = 3,
    SO_SNDBUF      = 7,
    SO_RCVBUF      = 8,
    SHUT_RD        = 0,
    SHUT_WR        = 1,
    SHUT_RDWR      = 2,
};

/* OP_STATX: preserve the final directory entry instead of following it. */
enum { STATX_NOFOLLOW = 1u << 0 };

/* OP_READDIR op_flags.  By default sqe.offset points at the pathname and
   the entry index is in sqe.fd (the legacy path form).  With this flag
   sqe.fd is an open directory descriptor and sqe.offset is the index, so
   the stream reads through the handle opened by opendir() instead of
   resolving a caller-held path again. */
enum { ASTIA_AIO_READDIR_FD = 1u << 0 };

/* OP_UNLINKAT op_flags: remove a directory instead of a non-directory. */
enum { UNLINKAT_REMOVEDIR = 1u << 0 };

/* astia_sqe.flags */
enum {
    SQF_FIXED_FILE  = 1u << 0, /* fd = index into registered files */
    SQF_FIXED_BUF   = 1u << 1, /* aux = index into registered buffers */
    SQF_IO_LINK     = 1u << 2, /* run after the previous SQE completes */
    SQF_IO_HARDLINK = 1u << 3, /* link survives a failed predecessor */
    SQF_IO_DRAIN    = 1u << 4, /* start only after all older SQEs */
};

/* astia_cqe.flags */
enum {
    CQF_MORE = 1u << 0, /* multishot op: another CQE will follow */
};

/* astia_io_setup flags */
enum {
    SETUP_SQPOLL = 1u << 0, /* kernel polls the SQ (feature-gated) */
    SETUP_IOPOLL = 1u << 1, /* busy-poll device completions */
};

/* astia_rings.features */
enum {
    FEAT_SINGLE_MMAP = 1u << 0,
    FEAT_REG_FILES   = 1u << 1,
    FEAT_REG_BUFFERS = 1u << 2,
    FEAT_LINK        = 1u << 3,
    FEAT_DRAIN       = 1u << 4,
    FEAT_SQPOLL      = 1u << 5,
    FEAT_IOPOLL      = 1u << 6,
    FEAT_MULTISHOT   = 1u << 7,
};

/* ring flags: astia_sq_ring.flags (transient) and
   astia_rings.flags (sticky) */
enum {
    RINGF_NEED_WAKEUP = 1u << 0, /* SQPOLL needs a kick */
    RINGF_OVERFLOW    = 1u << 1, /* diagnostic overflow state */
};

/* astia_io_register opcodes (ptr = array, nr = element count) */
enum {
    REG_FILES     = 1, /* const int32_t fd[]   */
    REG_BUFFERS   = 2, /* const astia_reg_buf[] */
    UNREG_FILES   = 3,
    UNREG_BUFFERS = 4,
};

/* ------------------------------------------------------------------ */
/* constants for the op flag words                                     */
/* ------------------------------------------------------------------ */

/* open flags (OPENAT op_flags).  These are the ring wire values; they are
   deliberately not the native libc fcntl.h values. */
enum {
    ASTIA_AIO_O_RDONLY    = 0x00,
    ASTIA_AIO_O_WRONLY    = 0x01,
    ASTIA_AIO_O_RDWR      = 0x02,
    ASTIA_AIO_O_ACCMODE   = 0x03,
    ASTIA_AIO_O_CREAT     = 0x04,
    ASTIA_AIO_O_EXCL      = 0x08,
    ASTIA_AIO_O_TRUNC     = 0x10,
    ASTIA_AIO_O_APPEND    = 0x20,
    ASTIA_AIO_O_DIRECTORY = 0x40,
    ASTIA_AIO_O_CLOEXEC   = 0x80,
    ASTIA_AIO_O_NONBLOCK  = 0x100,
};

#define ASTIA_AIO_POSITIONED (1u << 31)

/* Compatibility spellings for freestanding kernel users which do not have
   a libc fcntl.h in scope. */
#ifndef O_DIRECTORY
enum {
    O_RDONLY    = ASTIA_AIO_O_RDONLY,
    O_WRONLY    = ASTIA_AIO_O_WRONLY,
    O_RDWR      = ASTIA_AIO_O_RDWR,
    O_ACCMODE   = ASTIA_AIO_O_ACCMODE,
    O_CREAT     = ASTIA_AIO_O_CREAT,
    O_EXCL      = ASTIA_AIO_O_EXCL,
    O_TRUNC     = ASTIA_AIO_O_TRUNC,
    O_APPEND    = ASTIA_AIO_O_APPEND,
    O_DIRECTORY = ASTIA_AIO_O_DIRECTORY,
    O_CLOEXEC   = ASTIA_AIO_O_CLOEXEC,
};
#endif

/* poll events (POLL_ADD op_flags; revents in cqe.result).  libc's poll.h
   exposes these as macros, so do not redeclare identifiers when included
   after that header. */
#ifndef POLLIN
enum {
    POLLIN   = 0x001,
    POLLPRI  = 0x002,
    POLLOUT  = 0x004,
    POLLERR  = 0x008,
    POLLHUP  = 0x010,
    POLLNVAL = 0x020,
};
#endif

/* wait options (WAIT op_flags) */
/* Keep the traditional names usable without colliding with libc's macros. */
#ifndef WNOHANG
#define WNOHANG ASTIA_WNOHANG /* complete at once with pid or -EAGAIN */
#endif
#ifndef WUNTRACED
#define WUNTRACED ASTIA_WUNTRACED
#endif
#ifndef WCONTINUED
#define WCONTINUED ASTIA_WCONTINUED
#endif

/* sleep options (SLEEP op_flags) */
enum {
    SLEEP_ABSOLUTE = 1u << 0, /* offset is a monotonic deadline */
};

/* spawn flags (astia_spawn.flags) */
enum {
    SPAWN_DETACHED = 1u << 0, /* no one waits; WAIT is a no-op */
};

/* rename options (RENAMEAT op_flags) */
enum {
    RENAME_NOREPLACE = 1u << 0,
    RENAME_EXCHANGE  = 1u << 1,
};

/* address families (astia_sockaddr.family, out) */
enum {
    AF_UNSPEC = 0,
    AF_LOCAL = 1,
    AF_INET = 2,
    AF_AX25 = 3,
    AF_IPX = 4,
    AF_APPLETALK = 5,
    AF_NETROM = 6,
    AF_BRIDGE = 7,
    AF_ATMPVC = 8,
    AF_X25 = 9,
    AF_INET6 = 10,
    AF_ROSE = 11,
    AF_DECNET = 12,
    AF_NETBEUI = 13,
    AF_SECURITY = 14,
    AF_KEY = 15,
    AF_NETLINK = 16,
    AF_PACKET = 17,
    AF_ASH = 18,
    AF_ECONET = 19,
    AF_ATMSVC = 20,
    AF_RDS = 21,
    AF_SNA = 22,
    AF_IRDA = 23,
    AF_PPPOX = 24,
    AF_WANPIPE = 25,
    AF_LLC = 26,
    AF_IB = 27,
    AF_MPLS = 28,
    AF_CAN = 29,
    AF_TIPC = 30,
    AF_BLUETOOTH = 31,
    AF_IUCV = 32,
    AF_RXRPC = 33,
    AF_ISDN = 34,
    AF_PHONET = 35,
    AF_IEEE802154 = 36,
    AF_CAIF = 37,
    AF_ALG = 38,
    AF_NFC = 39,
    AF_VSOCK = 40,
    AF_KCM = 41,
    AF_QIPCRTR = 42,
    AF_SMC = 43,
    AF_XDP = 44,
    AF_MCTP = 45,
};

#ifndef AF_UNIX
#define AF_UNIX AF_LOCAL
#endif

#ifndef AT_FDCWD
enum { AT_FDCWD = -100 };
#endif

#ifndef AT_SYMLINK_NOFOLLOW
enum { AT_SYMLINK_NOFOLLOW = 0x100 };
#endif

/* OP_UTIMENSAT times[] selectors, matching the libc UTIME_* values. */
enum {
    ASTIA_UTIME_NOW  = (1 << 30) - 1,
    ASTIA_UTIME_OMIT = (1 << 30) - 2,
};

/* file offset meaning "append" (uint64 value, hence a macro) */
#define OFF_APPEND ((uint64_t) 0xFFFFFFFFFFFFFFFFull)

/* ------------------------------------------------------------------ */
/* ring management wrappers — the SYS_IO_* entries of the table        */
/* ------------------------------------------------------------------ */

static inline long astia_io_setup(uint32_t entries, uint32_t flags, astia_rings** out) {
    return astia_raw6(SYS_IO_SETUP, (long) entries, (long) flags, (long) out, 0, 0, 0);
}

static inline long astia_io_destroy(astia_rings* ring) {
    return astia_raw6(SYS_IO_DESTROY, (long) ring, 0, 0, 0, 0, 0);
}

/* Submit up to to_submit SQEs from the shared tail and return immediately.
   Each accepted, published SQE is consumed exactly once as the kernel
   advances SQ head; repeating this kick without publishing does not resubmit
   it. Distinct duplicate SQEs remain distinct operations. The CQ is the
   source of truth for operation completion. */
static inline long astia_io_submit(astia_rings* ring, uint32_t to_submit) {
    return astia_raw6(SYS_IO_SUBMIT, (long) ring, (long) to_submit, 0, 0, 0, 0);
}

static inline long astia_io_register(astia_rings* ring, uint32_t opcode, const void* ptr,
                                     uint32_t nr) {
    return astia_raw6(SYS_IO_REGISTER, (long) ring, (long) opcode, (long) ptr, (long) nr,
                      0, 0);
}

/* ------------------------------------------------------------------ */
/* userspace ring manipulation — plain memory ops, no syscalls.        */
/*                                                                     */
/* Ordering: the kernel reads the SQE before the release-store to      */
/* tail; it release-stores the CQE before cq.tail, which we acquire-   */
/* load before touching the CQE.  Single producer (SQ) and single      */
/* reaper (CQ) per ring; cross-thread use needs external locking.      */
/* ------------------------------------------------------------------ */

static inline astia_sq_ring* astia_rings_sq(astia_rings* r) {
    return (astia_sq_ring*) ((char*) r + r->sq_off);
}

static inline astia_cq_ring* astia_rings_cq(astia_rings* r) {
    return (astia_cq_ring*) ((char*) r + r->cq_off);
}

static inline astia_sqe* astia_rings_sqes(astia_rings* r) {
    return (astia_sqe*) ((char*) r + r->sqes_off);
}

static inline astia_cqe* astia_rings_cqes(astia_rings* r) {
    return (astia_cqe*) ((char*) r + r->cqes_off);
}

/* Unsubmitted SQ space (0 when the kernel must drain: kick io_submit). */
static inline uint32_t astia_sq_space(astia_rings* r) {
    astia_sq_ring* sq   = astia_rings_sq(r);
    uint32_t       tail = __atomic_load_n(&sq->tail, __ATOMIC_RELAXED);
    uint32_t       head = __atomic_load_n(&sq->head, __ATOMIC_ACQUIRE);

    return sq->entries - (tail - head);
}

/* Next free SQE slot, or NULL when full (kick io_submit, then retry).
   Fill it completely, then astia_sq_publish() — never publish first. */
static inline astia_sqe* astia_sqe_slot(astia_rings* r) {
    astia_sq_ring* sq   = astia_rings_sq(r);
    uint32_t       tail = __atomic_load_n(&sq->tail, __ATOMIC_RELAXED);

    if (tail - __atomic_load_n(&sq->head, __ATOMIC_ACQUIRE) >= sq->entries)
        return NULL;

    return &astia_rings_sqes(r)[tail & sq->mask];
}

/* Publish n filled slots; takes effect for the kernel at the next
   astia_io_submit() (or immediately for an SQPOLL thread). */
static inline void astia_sq_publish(astia_rings* r, uint32_t n) {
    astia_sq_ring* sq   = astia_rings_sq(r);
    uint32_t       tail = __atomic_load_n(&sq->tail, __ATOMIC_RELAXED);

    __atomic_store_n(&sq->tail, tail + n, __ATOMIC_RELEASE);
}

/* Unreaped completions available right now. */
static inline uint32_t astia_cq_ready(astia_rings* r) {
    astia_cq_ring* cq = astia_rings_cq(r);

    return __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE) -
           __atomic_load_n(&cq->head, __ATOMIC_RELAXED);
}

/* Oldest un-reaped completion, or NULL. */
static inline astia_cqe* astia_cq_peek(astia_rings* r) {
    astia_cq_ring* cq = astia_rings_cq(r);

    if (__atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE) ==
        __atomic_load_n(&cq->head, __ATOMIC_RELAXED))
        return NULL;

    return &astia_rings_cqes(r)[cq->head & cq->mask];
}

/* Hand n CQE slots back to the kernel after reading them. */
static inline void astia_cq_consume(astia_rings* r, uint32_t n) {
    astia_cq_ring* cq   = astia_rings_cq(r);
    uint32_t       head = __atomic_load_n(&cq->head, __ATOMIC_RELAXED);

    __atomic_store_n(&cq->head, head + n, __ATOMIC_RELEASE);

    if (n != 0)
        __atomic_store_n(&cq->notify_ack,
                         __atomic_load_n(&cq->notify_generation, __ATOMIC_ACQUIRE),
                         __ATOMIC_RELEASE);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
