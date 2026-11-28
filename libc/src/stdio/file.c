#include "internal/file_port.h"
#include "internal/path.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * astia libc — FILE streams.
 *
 * Synchronous API on top of an asynchronous backend: every call either
 * finishes the job (bytes in the user buffer, or submitted *and*
 * completed on the device) or fails with errno/ferror set.  The backend
 * is src/internal/file_port.h — one function per blocking primitive; on astia
 * each is a single SQE submitted and waited for inside the call.
 *
 * Buffering: one buffer per stream, direction-tagged.  A refill always
 * starts exactly at `pos` (the logical position), so an occupied read
 * buffer always means "file[pos - cur .. pos - cur + len)" with the
 * cursor at `pos` — there is no second offset to keep in sync.  Writes
 * buffer the bytes for [pos - len, pos) and flush forwards them
 * explicitly, so seeking needs no kernel-side seek at all (append
 * streams pass FIO_OFF_APPEND instead and let the kernel place them).
 *
 * Switching direction (read -> write or back) discards the other side's
 * buffer: pending writes are flushed first, read-ahead is dropped (it
 * was never consumed, so `pos` already points past it).
 *
 * Not thread-safe yet: FILEs, the open-file list, and the ring (single
 * producer by ABI) all need the futex lock that arrives with threads —
 * errno, being _Thread_local, is already fine.
 */

enum { ST_EOF = 1, ST_ERR = 2 };

enum astia_file_kind { AF_FD = 0, AF_MEM = 1 };

struct astia_memstream {
    char**  bufp;
    size_t* sizep;
    char*   data;
    size_t  length; /* logical buffer length, excluding the terminating NUL */
    size_t  capacity;
};

struct astia_file {
    int                    fd;
    int                    io_flags;  /* FIO_READ | FIO_WRITE | FIO_APPEND */
    unsigned               state;     /* ST_EOF | ST_ERR */
    int                    buf_mode;  /* _IOFBF | _IOLBF | _IONBF */
    int                    heap;      /* the struct itself came from fopen */
    int                    buf_owned; /* the buffer came from malloc (not static/user) */
    int                    reading;   /* buffer currently holds read-ahead, not writes */
    long                   pos;       /* logical position: what ftell reports */
    unsigned char*         buf;
    size_t                 cap; /* buffer capacity; 0 == unbuffered */
    size_t                 len; /* read: valid bytes; write: pending bytes */
    size_t                 cur; /* read cursor inside buf */
    int                    has_pushback;
    int                    pushback_backed; /* pos was decremented for it */
    unsigned char          pushback;
    enum astia_file_kind   kind;
    struct astia_memstream mem;
    struct astia_file*     next; /* open-file list, for fflush(NULL) */
};

/* Standard streams: static storage, freed by nobody. */
static unsigned char stdin_buf[BUFSIZ];
static unsigned char stdout_buf[BUFSIZ];
/* stderr has no buffer: unbuffered from the first byte. */

static FILE file_stdin = {
    .fd       = 0,
    .io_flags = FIO_READ,
    .buf_mode = _IOFBF,
    .buf      = stdin_buf,
    .cap      = BUFSIZ,
};
static FILE file_stdout = {
    .fd       = 1,
    .io_flags = FIO_WRITE,
    .buf_mode = _IOLBF,
    .buf      = stdout_buf,
    .cap      = BUFSIZ,
};
static FILE file_stderr = {
    .fd       = 2,
    .io_flags = FIO_WRITE,
    .buf_mode = _IONBF,
    .buf      = NULL,
    .cap      = 0,
};

FILE* stdin  = &file_stdin;
FILE* stdout = &file_stdout;
FILE* stderr = &file_stderr;

static FILE* g_open; /* fopen'd streams, for fflush(NULL) */

static size_t io_cap(size_t n) {
    return n > FIO_MAXIO ? FIO_MAXIO : n;
}

/* Memory sinks are a second, local backend for the same stream machinery:
 * writes go to a caller-visible malloc/realloc buffer instead of a
 * descriptor.  The fd-backed path below is otherwise untouched — these
 * three primitives are the only places that discriminate. */
static int mem_reserve(FILE* f, size_t need) {
    size_t cap;

    char* grown;

    if (need <= f->mem.capacity)
        return 0;

    cap = f->mem.capacity == 0 ? 1 : f->mem.capacity;

    while (cap < need) {
        if (cap > (size_t) -1 / 2) {
            cap = need;

            break;
        }

        cap *= 2;
    }

    grown = realloc(f->mem.data, cap);
    if (grown == NULL) {
        errno = ENOMEM;
        f->state |= ST_ERR;

        return -1;
    }

    f->mem.data     = grown;
    f->mem.capacity = cap;
    *f->mem.bufp    = grown; /* keep a published pointer current */

    return 0;
}

static long mem_write(FILE* f, size_t off, const void* src, size_t n) {
    const unsigned char* s = src;

    size_t end = off + n;

    if (end < off || end == (size_t) -1)
        return -ENOMEM;

    if (mem_reserve(f, end + 1) < 0)
        return -ENOMEM;

    if (off > f->mem.length)
        memset(f->mem.data + f->mem.length, 0, off - f->mem.length);

    memcpy(f->mem.data + off, s, n);

    if (end > f->mem.length)
        f->mem.length = end;

    f->mem.data[f->mem.length] = '\0';

    return (long) n;
}

/* Update the caller's view after fflush/fclose.  Seeking past the old end
 * extends with zero bytes here (a permitted implementation choice); the
 * reported size is the smaller of the new length and the current position,
 * as Issue 8 requires. */
static int mem_publish(FILE* f, int terminate) {
    size_t pos = (size_t) f->pos;
    size_t size;

    if (pos > f->mem.length) {
        if (mem_reserve(f, pos + 1) < 0)
            return -1;

        memset(f->mem.data + f->mem.length, 0, pos - f->mem.length);

        f->mem.length    = pos;
        f->mem.data[pos] = '\0';
    }

    size = pos < f->mem.length ? pos : f->mem.length;

    if (terminate)
        f->mem.data[size] = '\0';

    *f->mem.bufp  = f->mem.data;
    *f->mem.sizep = size;

    return 0;
}

static long file_sink_write(FILE* f, uint64_t off, const void* buf, size_t n) {
    if (f->kind == AF_MEM) {
        if (off > (uint64_t) (size_t) -1)
            return -ENOMEM;

        return mem_write(f, (size_t) off, buf, n);
    }

    return __astia_fio_write(f->fd, off, buf, n);
}

static long file_sink_size(FILE* f, uint64_t* out) {
    if (f->kind == AF_MEM) {
        *out = (uint64_t) f->mem.length;

        return 0;
    }

    return __astia_fio_size(f->fd, out);
}

/* Append writes land at the kernel's end of file, not at `pos`, so the
 * logical position must be re-anchored after them.  Without this an "a+"
 * stream that started at 0 reported the byte count instead of the offset. */
static int append_resync(FILE* f) {
    uint64_t sz;

    long rc = file_sink_size(f, &sz);

    if (rc < 0) {
        errno = (int) -rc;
        f->state |= ST_ERR;

        return -1;
    }

    if (sz > (uint64_t) LONG_MAX) {
        errno = EOVERFLOW;
        f->state |= ST_ERR;

        return -1;
    }

    f->pos = (long) sz;

    return 0;
}

char* cuserid(char* buffer) {
    static char fallback[] = "astia";

    if (buffer == NULL)
        return fallback;

    buffer[0] = 'a';
    buffer[1] = 's';
    buffer[2] = 't';
    buffer[3] = 'i';
    buffer[4] = 'a';
    buffer[5] = '\0';

    return buffer;
}

FILE* popen(const char* command, const char* mode) {
    (void) command;
    (void) mode;

    return NULL;
}

int pclose(FILE* stream) {
    (void) stream;

    return -1;
}

/* Forward the pending write buffer; on error keep the unwritten tail at
 * the front of the buffer — pos - len still resolves to the right
 * offset, so a clearerr + retry continues where it stopped. */
static int flush_pending(FILE* f) {
    size_t done = 0;

    if (f->reading || f->len == 0)
        return 0;

    while (done < f->len) {
        uint64_t off;

        long r;

        if (f->io_flags & FIO_APPEND)
            off = FIO_OFF_APPEND;
        else
            off = (uint64_t) (f->pos - (long) f->len + (long) done);

        r = file_sink_write(f, off, f->buf + done, io_cap(f->len - done));
        if (r <= 0) {
            int err = (r < 0) ? (int) -r : EIO;

            if (done > 0) {
                memmove(f->buf, f->buf + done, f->len - done);

                f->len -= done;
            }

            errno = err;
            f->state |= ST_ERR;

            return -1;
        }

        done += (size_t) r;
    }

    f->len = 0;

    if ((f->io_flags & FIO_APPEND) && append_resync(f) < 0)
        return -1;

    return 0;
}

/* Throw away read-ahead.  `pos` counts only consumed bytes, so it is
 * already the right resume point — nothing is lost. */
static void drop_read(FILE* f) {
    f->len             = 0;
    f->cur             = 0;
    f->reading         = 0;
    f->has_pushback    = 0;
    f->pushback_backed = 0;
}

static size_t stream_read(FILE* f, void* dst, size_t n) {
    unsigned char* d = dst;

    size_t got = 0;

    if (f->fd < 0 || !(f->io_flags & FIO_READ)) {
        errno = EBADF;
        f->state |= ST_ERR;

        return 0;
    }

    if (f->state & ST_ERR)
        return 0;

    if (!f->reading && f->len > 0) { /* direction switch: writes first */
        if (flush_pending(f) < 0)
            return 0;
    }

    f->reading = 1;

    while (got < n) {
        if (f->has_pushback) {
            d[got++]        = f->pushback;
            f->has_pushback = 0;

            if (f->pushback_backed)
                f->pos++;

            f->pushback_backed = 0;

            continue;
        }

        if (f->cur == f->len) { /* buffer empty */
            long r;

            if (f->state & ST_EOF)
                break;

            if (f->cap == 0 || n - got >= f->cap) {
                /* big request (or unbuffered): read straight past the
                   buffer into the caller's memory */
                r = __astia_fio_read(f->fd, (uint64_t) f->pos, d + got, io_cap(n - got));
                if (r < 0) {
                    errno = (int) -r;
                    f->state |= ST_ERR;

                    break;
                }

                if (r == 0) {
                    f->state |= ST_EOF;

                    break;
                }

                got += (size_t) r;
                f->pos += (long) r;

                continue;
            }

            r = __astia_fio_read(f->fd, (uint64_t) f->pos, f->buf, io_cap(f->cap));
            if (r < 0) {
                errno = (int) -r;
                f->state |= ST_ERR;

                break;
            }

            if (r == 0) {
                f->state |= ST_EOF;

                break;
            }

            f->len = (size_t) r;
            f->cur = 0;
        }

        {
            size_t avail = f->len - f->cur;
            size_t take  = n - got < avail ? n - got : avail;

            memcpy(d + got, f->buf + f->cur, take);

            f->cur += take;
            f->pos += (long) take;
            got += take;
        }
    }

    return got;
}

static size_t stream_write(FILE* f, const void* src, size_t n) {
    const unsigned char* s = src;

    size_t put = 0;

    if ((f->kind == AF_FD && f->fd < 0) || !(f->io_flags & FIO_WRITE)) {
        errno = EBADF;
        f->state |= ST_ERR;

        return 0;
    }

    if (f->state & ST_ERR)
        return 0;

    if (f->reading)
        drop_read(f); /* direction switch: read-ahead was unconsumed */

    if ((f->io_flags & FIO_APPEND) && f->len == 0 && append_resync(f) < 0)
        return 0;

    if (f->cap == 0) { /* unbuffered: straight through, offset explicit */
        while (put < n) {
            uint64_t off = (f->io_flags & FIO_APPEND) ? FIO_OFF_APPEND
                                                      : (uint64_t) (f->pos + (long) put);

            long r = file_sink_write(f, off, s + put, io_cap(n - put));
            if (r <= 0) {
                errno = (r < 0) ? (int) -r : EIO;
                f->state |= ST_ERR;

                break;
            }

            put += (size_t) r;
            f->pos += (long) r;
        }

        return put;
    }

    while (put < n) {
        size_t take;

        if (f->len == f->cap && flush_pending(f) < 0)
            break;

        take = n - put;

        if (take > f->cap - f->len)
            take = f->cap - f->len;

        memcpy(f->buf + f->len, s + put, take);

        f->len += take;
        f->pos += (long) take;
        put += take;

        if (f->buf_mode == _IOLBF && memchr(f->buf + f->len - take, '\n', take) != NULL &&
            flush_pending(f) < 0)
            break;
    }

    return put;
}

/* "r" | "w" | "a", then any order of optional 'b' (no-op), '+' and —
   for the creating modes — 'x' (C11 exclusive: fail on existing file). */
static int parse_mode(const char* mode, int* out) {
    int plus = 0;
    int bin  = 0;
    int excl = 0;
    int flags;

    const char* p;

    if (mode == NULL || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a'))
        return -1;

    for (p = mode + 1; *p != '\0'; p++) {
        if (*p == 'b') {
            if (bin)
                return -1;

            bin = 1;
        }
        else if (*p == '+') {
            if (plus)
                return -1;

            plus = 1;
        }
        else if (*p == 'x') {
            if (excl)
                return -1;

            excl = 1;
        }
        else {
            return -1;
        }
    }

    if (mode[0] == 'r') {
        if (excl) /* exclusive creation needs a creating mode */
            return -1;

        flags = plus ? (FIO_READ | FIO_WRITE) : FIO_READ;
    }
    else {
        flags = plus ? (FIO_READ | FIO_WRITE) : FIO_WRITE;
        flags |= FIO_CREAT;
        flags |= (mode[0] == 'a') ? FIO_APPEND : FIO_TRUNC;

        if (excl)
            flags |= FIO_EXCL;
    }

    *out = flags;

    return 0;
}

static FILE* alloc_stream(int fd, int flags, long init_pos) {
    FILE* f = malloc(sizeof *f);
    if (f != NULL)
        f->buf = malloc(BUFSIZ);

    if (f == NULL || f->buf == NULL) {
        free(f);

        errno = ENOMEM;

        return NULL;
    }

    f->fd              = fd;
    f->io_flags        = flags;
    f->state           = 0;
    f->buf_mode        = _IOFBF;
    f->heap            = 1;
    f->buf_owned       = 1;
    f->reading         = 0;
    f->pos             = init_pos;
    f->cap             = BUFSIZ;
    f->len             = 0;
    f->cur             = 0;
    f->has_pushback    = 0;
    f->pushback_backed = 0;
    f->kind            = AF_FD;
    f->next            = g_open;
    g_open             = f;

    return f;
}

FILE* open_memstream(char** bufp, size_t* sizep) {
    FILE* f;
    char* data;

    if (bufp == NULL || sizep == NULL) {
        errno = EINVAL;

        return NULL;
    }

    data = malloc(1);
    if (data == NULL) {
        errno = ENOMEM;

        return NULL;
    }

    data[0] = '\0';
    f       = malloc(sizeof *f);
    if (f == NULL) {
        free(data);

        errno = ENOMEM;

        return NULL;
    }

    f->buf = malloc(BUFSIZ);

    if (f->buf == NULL) {
        free(data);
        free(f);

        errno = ENOMEM;

        return NULL;
    }

    f->fd              = -1;
    f->io_flags        = FIO_WRITE;
    f->state           = 0;
    f->buf_mode        = _IOFBF;
    f->heap            = 1;
    f->buf_owned       = 1;
    f->reading         = 0;
    f->pos             = 0;
    f->cap             = BUFSIZ;
    f->len             = 0;
    f->cur             = 0;
    f->has_pushback    = 0;
    f->pushback_backed = 0;
    f->kind            = AF_MEM;
    f->mem.bufp        = bufp;
    f->mem.sizep       = sizep;
    f->mem.data        = data;
    f->mem.length      = 0;
    f->mem.capacity    = 1;
    f->next            = g_open;
    g_open             = f;

    /* POSIX: the caller can read a valid empty string immediately, not
       only after the first fflush/fclose. */
    *bufp  = data;
    *sizep = 0;

    return f;
}

FILE* fdopen(int fd, const char* mode) {
    int  flags;
    long status;
    long position;
    int  access;
    int  requested;

    if (mode == NULL || parse_mode(mode, &flags) < 0 || (flags & FIO_EXCL) != 0) {
        errno = EINVAL;

        return NULL;
    }

    flags &= ~(FIO_CREAT | FIO_EXCL | FIO_TRUNC);
    status = __astia_fio_fcntl(fd, F_GETFL, 0);
    if (status < 0) {
        errno = (int) -status;

        return NULL;
    }

    access    = (int) status & O_ACCMODE;
    requested = flags & (FIO_READ | FIO_WRITE);

    if ((requested == FIO_READ && access == O_WRONLY) ||
        (requested == FIO_WRITE && access == O_RDONLY) ||
        (requested == (FIO_READ | FIO_WRITE) && access != O_RDWR)) {
        errno = EINVAL;

        return NULL;
    }

    if ((flags & FIO_APPEND) && !(status & O_APPEND) &&
        fcntl(fd, F_SETFL, (int) status | O_APPEND) < 0)
        return NULL;

    if (status & O_APPEND)
        flags |= FIO_APPEND;

    flags &= ~(FIO_CREAT | FIO_EXCL | FIO_TRUNC);

    position = __astia_fio_lseek(fd, 0, SEEK_CUR);

    if (position < 0 && position != -ESPIPE) {
        errno = (int) -position;

        return NULL;
    }

    if (position < 0)
        position = 0;

    return alloc_stream(fd, flags, position);
}

static void list_remove(FILE* f) {
    FILE** pp = &g_open;

    while (*pp != NULL) {
        if (*pp == f) {
            *pp = f->next;

            return;
        }

        pp = &(*pp)->next;
    }
}

FILE* fopen(const char* path, const char* mode) {
    FILE* f;

    int  flags;
    long fd;
    long init_pos = 0;

    if (path == NULL || parse_mode(mode, &flags) < 0) {
        errno = EINVAL;

        return NULL;
    }

    {
        char resolved[256];

        if (__astia_resolve_path(path, resolved, sizeof resolved) != 0)
            return NULL;

        fd = __astia_fio_open(resolved, flags, 0666);
    }

    if (fd < 0) {
        errno = (int) -fd;

        return NULL;
    }

    /* POSIX: a pure append stream starts at the end of the file, so
       ftell agrees with SEEK_END from the first call; "a+" keeps 0
       because its reads start at the beginning.  Append writes re-anchor
       pos to the kernel's end of file as they happen. */
    if ((flags & FIO_APPEND) && !(flags & FIO_READ)) {
        uint64_t sz;

        long rc = __astia_fio_size((int) fd, &sz);

        if (rc < 0 || sz > (uint64_t) LONG_MAX) {
            int err = rc < 0 ? (int) -rc : EOVERFLOW;

            (void) __astia_fio_close((int) fd);

            errno = err;

            return NULL;
        }

        init_pos = (long) sz;
    }

    f = alloc_stream((int) fd, flags, init_pos);
    if (f == NULL) {
        (void) __astia_fio_close((int) fd);

        return NULL;
    }

    return f;
}

int fclose(FILE* f) {
    int rc;
    int crc;

    if (f == NULL || (f->kind == AF_FD && f->fd < 0))
        return EOF;

    /* flush first (a failed flush still gets the fd closed: the caller
       learns about it through EOF, the descriptor must not leak) */
    rc = fflush(f);

    if (f->kind == AF_MEM) {
        if (rc == 0 && mem_publish(f, 1) < 0)
            rc = EOF;

        crc = 0;
    }
    else {
        crc = (int) __astia_fio_close(f->fd);

        if (crc < 0 && rc == 0)
            errno = (int) -crc; /* the close failed while the flush
                                   succeeded: it must name itself (a flush
                                   failure already set errno).  The fd
                                   number is lost either way — ISO
                                   deallocates the stream regardless. */
    }

    if (f->heap)
        list_remove(f);

    if (f->buf_owned)
        free(f->buf);

    f->fd  = -1;
    f->len = 0;
    f->cur = 0;

    if (f->heap)
        free(f); /* static streams survive with fd == -1 */

    return (rc != 0 || crc < 0) ? EOF : 0;
}

static int flush_fd_streams(int fd) {
    FILE* it;

    int rc = 0;

    if (file_stdin.fd == fd && fflush(&file_stdin) != 0)
        rc = EOF;

    if (file_stdout.fd == fd && fflush(&file_stdout) != 0)
        rc = EOF;

    if (file_stderr.fd == fd && fflush(&file_stderr) != 0)
        rc = EOF;

    for (it = g_open; it != NULL; it = it->next) {
        if (it->fd == fd && fflush(it) != 0)
            rc = EOF;
    }

    return rc;
}

static void retarget_stream(FILE* f, int fd, int flags, long position) {
    if (f->fd != fd)
        return;

    f->fd              = fd;
    f->io_flags        = flags;
    f->state           = 0;
    f->reading         = 0;
    f->pos             = position;
    f->len             = 0;
    f->cur             = 0;
    f->has_pushback    = 0;
    f->pushback_backed = 0;
}

int __astia_stdio_dup2(int old_fd, int new_fd) {
    long source_status;
    long new_status;
    long result;
    long position;
    int  stream_flags;

    if (new_fd < 0) {
        errno = EBADF;

        return -1;
    }

    source_status = __astia_fio_fcntl(old_fd, F_GETFL, 0);
    if (source_status < 0) {
        errno = (int) -source_status;

        return -1;
    }

    if (old_fd == new_fd)
        return new_fd;

    if (flush_fd_streams(new_fd) != 0)
        return -1;

    result = __astia_fio_dup2(old_fd, new_fd);
    if (result < 0) {
        errno = (int) -result;

        return -1;
    }

    new_status = __astia_fio_fcntl(new_fd, F_GETFL, 0);
    if (new_status < 0)
        new_status = source_status;

    stream_flags = ((int) new_status & O_ACCMODE) == O_RDWR     ? FIO_READ | FIO_WRITE
                   : ((int) new_status & O_ACCMODE) == O_WRONLY ? FIO_WRITE
                                                                : FIO_READ;

    if ((new_status & O_APPEND) != 0)
        stream_flags |= FIO_APPEND;

    position = __astia_fio_lseek(new_fd, 0, SEEK_CUR);
    if (position < 0)
        position = 0;

    retarget_stream(&file_stdin, new_fd, stream_flags, position);
    retarget_stream(&file_stdout, new_fd, stream_flags, position);
    retarget_stream(&file_stderr, new_fd, stream_flags, position);

    {
        FILE* it;

        for (it = g_open; it != NULL; it = it->next) {
            retarget_stream(it, new_fd, stream_flags, position);
        }
    }

    return new_fd;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    size_t got;

    if (f == NULL || size == 0 || nmemb == 0)
        return 0;

    if (nmemb > (size_t) -1 / size)
        return 0;

    got = stream_read(f, ptr, size * nmemb);

    return got / size; /* a torn trailing element is consumed, not counted */
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    size_t put;

    if (f == NULL || size == 0 || nmemb == 0)
        return 0;

    if (nmemb > (size_t) -1 / size)
        return 0;

    put = stream_write(f, ptr, size * nmemb);

    return put / size;
}

/* POSIX: a stream is seekable only when the object beneath it is.  Ask
 * the kernel itself with lseek(fd, 0, SEEK_CUR): regular files and
 * directories answer with their position, pipes, sockets and terminals
 * with ESPIPE.  The probe moves nothing when it succeeds.  Memory sinks
 * have their own position and are always seekable. */
static int stream_seekable(FILE* f) {
    long rc;

    if (f->kind == AF_MEM)
        return 1;

    rc = __astia_fio_lseek(f->fd, 0, SEEK_CUR);
    if (rc < 0) {
        errno = (int) -rc;

        return 0;
    }

    return 1;
}

int fseek(FILE* f, long offset, int whence) {
    long base;

    uint64_t target;

    if (f == NULL)
        return EOF;

    if (f->kind == AF_FD && f->fd < 0) {
        errno = EBADF;

        return EOF;
    }

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;

        return EOF;
    }

    /* The seekability check comes before any flush or drop: a failing
       seek must leave the stream exactly as it was, read-ahead and
       pending writes included. */
    if (!stream_seekable(f))
        return EOF;

    /* Pending bytes must land before anything is decided — SEEK_END
       reads the file size, which must already include them, and the
       flush offsets are derived from pos.  Read-ahead is discarded, not
       consumed (pos counts consumed bytes only), so a failure below has
       moved nothing observable. */
    if (!f->reading && flush_pending(f) < 0)
        return EOF;

    drop_read(f);

    switch (whence) {
    case SEEK_SET:
        base = 0;

        break;
    case SEEK_CUR:
        base = f->pos;

        break;
    default: { /* SEEK_END */
        uint64_t sz;

        long rc = file_sink_size(f, &sz);
        if (rc < 0) {
            errno = (int) -rc;

            return EOF;
        }

        if (sz > (uint64_t) LONG_MAX) {
            errno = EOVERFLOW;

            return EOF;
        }

        base = (long) sz;

        break;
    }
    }

    /* target = base + offset in wrapping unsigned arithmetic: the wrap
       is *defined*, so one range check catches both overflow
       directions — the signed add would be UB past LONG_MAX.  Anything
       outside [0, LONG_MAX] is EINVAL. */
    target = (uint64_t) base + (uint64_t) offset;

    if (target > (uint64_t) LONG_MAX) {
        errno = EINVAL;

        return EOF;
    }

    f->pos = (long) target;
    f->state &= ~ST_EOF; /* seeking re-opens the question of EOF */

    return 0;
}

long ftell(FILE* f) {
    if (f == NULL || (f->kind == AF_FD && f->fd < 0)) {
        errno = EBADF;

        return -1;
    }

    if (!stream_seekable(f))
        return -1;

    return f->pos; /* includes pending writes: they are logically placed */
}

int fseeko(FILE* f, off_t offset, int whence) {
    return fseek(f, (long) offset, whence);
}

off_t ftello(FILE* f) {
    return (off_t) ftell(f);
}

void rewind(FILE* f) {
    if (f == NULL)
        return;

    (void) fseek(f, 0, SEEK_SET);
    clearerr(f);
}

int fileno(FILE* f) {
    if (f == NULL)
        return -1;

    if (f->fd < 0) {
        errno = EBADF;

        return -1;
    }

    return f->fd;
}

int fflush(FILE* f) {
    if (f == NULL) {
        /* ISO C: flush every open *output* stream; input buffers of
           other streams are none of this call's business (POSIX). */
        FILE* it;

        int rc = 0;

        for (it = g_open; it != NULL; it = it->next) {
            if ((it->io_flags & FIO_WRITE) && fflush(it) != 0)
                rc = EOF;
        }

        if (file_stdout.fd >= 0 && fflush(&file_stdout) != 0)
            rc = EOF;

        if (file_stderr.fd >= 0 && fflush(&file_stderr) != 0)
            rc = EOF;

        return rc;
    }

    if (f->kind == AF_FD && f->fd < 0) {
        errno = EBADF;

        return EOF;
    }

    if (f->reading) { /* POSIX: explicit fflush on input discards it */
        drop_read(f);

        return 0;
    }

    if (flush_pending(f) < 0)
        return EOF;

    if (f->kind == AF_MEM && mem_publish(f, 0) < 0)
        return EOF;

    return 0;
}

int feof(FILE* f) {
    return (f == NULL || (f->state & ST_EOF) == 0) ? 0 : 1;
}

int ferror(FILE* f) {
    return (f == NULL || (f->state & ST_ERR) == 0) ? 0 : 1;
}

void clearerr(FILE* f) {
    if (f != NULL)
        f->state &= ~(ST_EOF | ST_ERR);
}

int fgetc(FILE* f) {
    unsigned char c;

    if (f == NULL || stream_read(f, &c, 1) != 1)
        return EOF;

    return (int) c;
}

int fputc(int c, FILE* f) {
    unsigned char b = (unsigned char) c;

    if (f == NULL || stream_write(f, &b, 1) != 1)
        return EOF;

    return (int) b;
}

int getc(FILE* f) {
    return fgetc(f);
}

int putc(int c, FILE* f) {
    return fputc(c, f);
}

int getchar(void) {
    return fgetc(stdin);
}

int ungetc(int c, FILE* f) {
    if (c == EOF || f == NULL || f->kind == AF_MEM || f->fd < 0 ||
        !(f->io_flags & FIO_READ)) {
        if (f != NULL && (f->kind == AF_MEM || f->fd >= 0) && !(f->io_flags & FIO_READ))
            errno = EBADF;

        return EOF;
    }

    if (f->has_pushback)
        return EOF;

    if (!f->reading && f->len > 0 && flush_pending(f) < 0)
        return EOF;

    f->reading         = 1;
    f->pushback        = (unsigned char) c;
    f->has_pushback    = 1;
    f->pushback_backed = f->pos > 0;

    if (f->pushback_backed)
        f->pos--;

    f->state &= ~ST_EOF;

    return (unsigned char) c;
}

int fgetpos(FILE* f, fpos_t* pos) {
    if (f == NULL || pos == NULL) {
        errno = EINVAL;

        return EOF;
    }

    if (f->kind == AF_FD && f->fd < 0) {
        errno = EBADF;

        return EOF;
    }

    if (!stream_seekable(f))
        return EOF;

    *pos = f->pos;

    return 0;
}

int fsetpos(FILE* f, const fpos_t* pos) {
    if (pos == NULL) {
        errno = EINVAL;

        return EOF;
    }

    return fseek(f, *pos, SEEK_SET);
}

char* fgets(char* s, int n, FILE* f) {
    int i = 0;

    if (s == NULL || f == NULL || n <= 0)
        return NULL;

    while (i < n - 1) {
        unsigned char c;

        if (stream_read(f, &c, 1) != 1)
            break;

        s[i++] = (char) c;

        if (c == '\n')
            break;
    }

    if (i != 0) {
        s[i] = '\0';

        return s;
    }

    /* Zero characters: only a genuine EOF or read error may say so —
       with n == 1 there was nothing to ask for and the stream is fine
       (glibc hands back an empty string in that case). */
    if (f->state & (ST_EOF | ST_ERR))
        return NULL;

    s[0] = '\0';

    return s;
}

int fputs(const char* s, FILE* f) {
    size_t n;

    if (s == NULL || f == NULL)
        return EOF;

    n = strlen(s);

    /* The state check carries the n == 0 case, where "wrote n bytes"
       and "failed outright" would otherwise look identical. */
    if (stream_write(f, s, n) != n || (f->state & ST_ERR) != 0)
        return EOF;

    return 0;
}

int setvbuf(FILE* f, char* buf, int mode, size_t size) {
    unsigned char* nb = NULL;

    if (f == NULL || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) {
        errno = EINVAL;

        return EOF;
    }

    if (f->kind == AF_FD && f->fd < 0) {
        errno = EBADF;

        return EOF;
    }

    /* current contents must not survive a buffering change */
    if (fflush(f) != 0)
        return EOF;

    if (mode == _IONBF) {
        f->buf_mode = mode;
        f->cap      = 0; /* the allocation stays; fclose still owns it */

        return 0;
    }

    if (buf == NULL) {
        nb = malloc(size == 0 ? 1 : size);
        if (nb == NULL) {
            errno = ENOMEM;

            return EOF;
        }
    }

    if (f->buf_owned)
        free(f->buf);

    f->buf       = buf != NULL ? (unsigned char*) buf : nb;
    f->buf_owned = (buf == NULL);
    f->cap       = size;
    f->buf_mode  = mode;
    f->len       = 0;
    f->cur       = 0;

    return 0;
}
