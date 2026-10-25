#define _POSIX_C_SOURCE 200809L

/*
 * Host port of src/file_port.h — the backend the *tests* link so the
 * whole FILE layer runs against real files on the build machine.
 *
 * This translation unit deliberately does NOT see <astia/api/aio.h>:
 * the two dialects both spell their open flags O_CREAT/O_TRUNC/... with
 * different values, so each side of the port speaks exactly one dialect
 * (FIO_* in the middle, from file_port.h).
 *
 * The ring backend returns the ABI's -errno; host syscalls set glibc's
 * thread-local errno.  <errno.h> on the include path is *ours* (TLS
 * __astia_errno), which glibc's open/pread never touch — so read the
 * host errno straight from the one symbol glibc documents for it.
 * Host errno values match <astia/api/errno.h> for everything the tests
 * compare against (the ABI copied the familiar POSIX/LINUX set).
 */

#include <sys/stat.h>    /* fstat */
#include <sys/syscall.h> /* SYS_readv/SYS_writev, for the raw host calls below */
#include <sys/types.h>   /* ssize_t/off_t: ours now, `long` like the host's */

#include <fcntl.h> /* host O_* dialect, open() */

/* The test include path supplies astia's headers.  Clear their public
 * convenience macros before file_port pulls in the raw ABI enums, then put
 * back the Linux values used by this host shim. */
#undef CLOCK_MONOTONIC
#undef CLOCK_REALTIME
#undef CLOCK_BOOTTIME
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_CREAT
#undef O_EXCL
#undef O_TRUNC
#undef O_APPEND

/* <unistd.h> is ours now (getpid/gettid), so the host syscalls this
 * port drives are declared by hand — same pattern as every other host
 * port in these tests.  readv/writev are the exception that proves the
 * rule: this libc exports both now, so a call by name would bind to
 * ours and recurse straight back into this port; they go by raw syscall
 * instead, where nothing can interpose. */
extern ssize_t read(int fd, void* buf, size_t count);
extern ssize_t write(int fd, const void* buf, size_t count);
extern ssize_t pread(int fd, void* buf, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
extern int     close(int fd);

/* ESPIPE and friends: the ABI's codes, numerically identical to the
   host's for everything this port compares (ENOENT, EINVAL, EBADF,
   ESPIPE all match Linux).  Bare `errno` stays untouched — this file
   reads the host's TLS through __errno_location above, never ours. */
#include "../src/file_port.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000

#include <errno.h>

extern int* __errno_location(void); /* glibc's errno, without its header */
#define HOST_ERRNO (*__errno_location())

/* The vector pair.  preadv/pwritev are host library calls with no name
 * of ours to collide with; readv/writev, for the reason above, are raw
 * host syscalls.  glibc's syscall() normalizes errors to -1 + its errno,
 * while the seam's contract is the ABI's negated code — host_vector()
 * converts, and SYS_readv/SYS_writev cannot legitimately return -1 any
 * other way. */
extern ssize_t preadv(int fd, const struct iovec* iov, int iovcnt, off_t offset);
extern ssize_t pwritev(int fd, const struct iovec* iov, int iovcnt, off_t offset);
extern long    syscall(long number, ...);

struct host_stat {
    unsigned long dev, ino, nlink;
    unsigned int mode, uid, gid;
    unsigned int pad;
    unsigned long rdev, size, blksize, blocks;
    long atime, atime_nsec, mtime, mtime_nsec, ctime, ctime_nsec;
    long unused[3];
};

static long host_vector(long number, int fd, const struct iovec* iov, uint32_t count) {
    long rc = syscall(number, fd, (long) (uintptr_t) iov, count, 0);

    return rc == -1 ? -HOST_ERRNO : rc;
}

long __astia_fio_open(const char* path, int flags, int mode) {
    int access = flags & (FIO_READ | FIO_WRITE);
    int hf;

    if (access == (FIO_READ | FIO_WRITE))
        hf = O_RDWR;
    else if (access == FIO_WRITE)
        hf = O_WRONLY;
    else
        hf = O_RDONLY;

    if (flags & FIO_CREAT)
        hf |= O_CREAT;
    if (flags & FIO_EXCL)
        hf |= O_EXCL;
    if (flags & FIO_TRUNC)
        hf |= O_TRUNC;
    if (flags & FIO_APPEND)
        hf |= O_APPEND;

    long fd = syscall(SYS_openat, -100, path, hf, mode);
    return fd >= 0 ? fd : -HOST_ERRNO;
}

long __astia_fio_read(int fd, uint64_t off, void* buf, size_t n) {
    ssize_t r = syscall(SYS_pread64, fd, buf, n, (off_t) off);

    if (r < 0 && HOST_ERRNO == ESPIPE) {
        /* not seekable (pipe/tty): the fd's own cursor is the only
           position there is — fine for forward-only std streams */
        r = syscall(SYS_read, fd, buf, n);
    }
    return r >= 0 ? (long) r : -HOST_ERRNO;
}

long __astia_fio_write(int fd, uint64_t off, const void* buf, size_t n) {
    ssize_t r;

    if (off == FIO_OFF_APPEND) {
        /* O_APPEND was set at open(): plain write appends, and pwrite
           would refuse UINT64_MAX as an offset anyway */
        r = syscall(SYS_write, fd, buf, n);
    }
    else {
        r = syscall(SYS_pwrite64, fd, buf, n, (off_t) off);
        if (r < 0 && HOST_ERRNO == ESPIPE)
            r = syscall(SYS_write, fd, buf, n);
    }
    return r >= 0 ? (long) r : -HOST_ERRNO;
}

long __astia_fio_readv(int fd, uint64_t off, const struct iovec* iov, uint32_t count) {
    ssize_t r;

    if (off == FIO_OFF_APPEND) /* the descriptor's own cursor */
        return host_vector(SYS_readv, fd, iov, count);
    r = preadv(fd, iov, (int) count, (off_t) off); /* not seekable: ESPIPE, as pread */
    return r >= 0 ? (long) r : -HOST_ERRNO;
}

long __astia_fio_writev(int fd, uint64_t off, const struct iovec* iov, uint32_t count) {
    ssize_t r;

    if (off == FIO_OFF_APPEND)
        return host_vector(SYS_writev, fd, iov, count);
    r = pwritev(fd, iov, (int) count, (off_t) off);
    return r >= 0 ? (long) r : -HOST_ERRNO;
}

long __astia_fio_close(int fd) {
    return syscall(SYS_close, fd) < 0 ? -HOST_ERRNO : 0;
}

long __astia_fio_size(int fd, uint64_t* out) {
    struct stat st;

    if (fstat(fd, &st) < 0)
        return -HOST_ERRNO;
    *out = (uint64_t) st.st_size;
    return 0;
}

long __astia_fio_stat(const char* path, void* out, uint32_t length) {
    struct host_stat hs;
    astia_stat* st = (astia_stat*)out;
    long rc;
    if (length != sizeof *st) return -EINVAL;
    rc = syscall(SYS_newfstatat, -100, path, &hs, 0);
    if (rc < 0) return -HOST_ERRNO;
    *st = (astia_stat){(uint64_t)hs.dev, (uint64_t)hs.ino, (uint64_t)hs.size,
                       (uint64_t)hs.blocks, (hs.mode & 0170000) == 0040000 ? 4 :
                       ((hs.mode & 0170000) == 0020000 ? 2 : 8), (uint32_t)hs.nlink,
                       hs.uid, hs.gid, hs.atime, hs.atime_nsec, hs.mtime,
                       hs.mtime_nsec, hs.ctime, hs.ctime_nsec, {0, 0, 0, 0}};
    return 0;
}

long __astia_fio_fstat(int fd, void* out, uint32_t length) {
    struct host_stat hs;
    astia_stat* st = (astia_stat*)out;
    if (length != sizeof *st) return -EINVAL;
    if (syscall(SYS_fstat, fd, &hs) < 0) return -HOST_ERRNO;
    *st = (astia_stat){(uint64_t)hs.dev, (uint64_t)hs.ino, (uint64_t)hs.size,
                       (uint64_t)hs.blocks, (hs.mode & 0170000) == 0040000 ? 4 :
                       ((hs.mode & 0170000) == 0020000 ? 2 : 8), (uint32_t)hs.nlink,
                       hs.uid, hs.gid, hs.atime, hs.atime_nsec, hs.mtime,
                       hs.mtime_nsec, hs.ctime, hs.ctime_nsec, {0, 0, 0, 0}};
    return 0;
}

long __astia_fio_mkdir(const char* path, int mode) {
    long rc = syscall(SYS_mkdir, path, mode);
    return rc < 0 ? -HOST_ERRNO : 0;
}

long __astia_fio_unlink(const char* path, int directory) {
    long rc = syscall(directory ? SYS_rmdir : SYS_unlink, path);
    return rc < 0 ? -HOST_ERRNO : 0;
}

void __astia_fio_prepare_exec(void) {}
long __astia_fio_readdir(const char* path, uint64_t index, astia_dirent* out) {
    (void)path; (void)index; (void)out; return -ENOSYS;
}
long __astia_fio_ioctl(int fd, uint64_t request, void* arg, size_t length) {
    (void)fd; (void)request; (void)arg; (void)length; return -ENOSYS;
}
long __astia_fio_pipe(int fds[2]) {
    long rc = syscall(SYS_pipe, fds);
    return rc < 0 ? -HOST_ERRNO : 0;
}
long __astia_fio_dup(int fd) {
    long rc = syscall(SYS_dup, fd);
    return rc < 0 ? -HOST_ERRNO : rc;
}
long __astia_fio_dup2(int old_fd, int new_fd) {
    long rc = syscall(SYS_dup2, old_fd, new_fd);
    return rc < 0 ? -HOST_ERRNO : rc;
}
