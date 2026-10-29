#pragma once

/*
 * src/file_port.h — the file I/O port (private to the libc).
 *
 * Blocking file primitives: one per blocking file operation of the userspace
 * ABI, and the scatter/gather pair beside the single-buffer one.  This
 * is a platform seam like the kernel's arch layer — the build picks
 * which translation unit defines these, they are not injected anywhere:
 *
 *   - src/backend_aio.c (astia): each call is one SQE on the async I/O
 *     ring, kicked and waited for inside the call — the sync API of
 *     <stdio.h> is built here, on top of the async backend.
 *   - tests/backend_host.c (host tests): POSIX pread/pwrite/fstat.
 *
 * Return convention, the ABI's: >= 0 is the payload (fd, byte count, 0),
 * a negative value is one of <astia/api/errno.h>'s codes negated.  The
 * FILE layer is the single place that folds that into errno.
 *
 * Contract for callers: n per call <= FIO_MAXIO, and an iovec list at
 * most the kernel's eight entries long.  FIO_OFF_APPEND as an `off`
 * argument means "the descriptor's own position rather than a byte
 * offset": it is the ABI's OFF_APPEND (~0), which the kernel resolves
 * to the open file's position.  A stream opened FIO_APPEND passes it
 * for its writes, and the vector pair below passes it always — a POSIX
 * vector call has no offset of its own.  (O_APPEND itself is not
 * implemented kernel-side yet: the position such a write lands at is
 * wherever the descriptor's cursor has reached, 0 for a fresh open.)
 */

#include <astia/api/aio.h>
#include <sys/uio.h>

#include <stddef.h>
#include <stdint.h>

enum { /* open flags — the subset every port can speak */
       FIO_READ   = 1u << 0,
       FIO_WRITE  = 1u << 1,
       FIO_CREAT  = 1u << 2,
       FIO_EXCL   = 1u << 3,
       FIO_TRUNC  = 1u << 4,
       FIO_APPEND = 1u << 5,
};

/* offset meaning "append"; == astia's OFF_APPEND by ABI design */
#define FIO_OFF_APPEND ((uint64_t) 0xFFFFFFFFFFFFFFFFull)

/* one call never carries more than this (fits a cqe.result comfortably) */
#define FIO_MAXIO ((size_t) 1u << 30)

#pragma GCC visibility push(default)
long __astia_fio_open(const char* path, int flags, int mode);
long __astia_fio_openat(int dirfd, const char* path, int flags, int mode);
long __astia_fio_read(int fd, uint64_t off, void* buf, size_t n);

struct astia_fio_batch_read {
    int      fd;
    uint64_t offset;
    void*    buffer;
    size_t   length;
    long     result;
};

/* Submit all reads as one ring batch and wait for all CQEs. */
long __astia_fio_read_batch(struct astia_fio_batch_read* requests, size_t count);
void __astia_fio_prepare_exec(void);
void __astia_fio_prepare_fork(void);
long __astia_fio_write(int fd, uint64_t off, const void* buf, size_t n);
long __astia_fio_readv(int fd, uint64_t off, const struct iovec* iov, uint32_t count);
long __astia_fio_writev(int fd, uint64_t off, const struct iovec* iov, uint32_t count);
long __astia_fio_close(int fd);
long __astia_fio_size(int fd, uint64_t* out); /* file length, for SEEK_END */
long __astia_fio_stat(const char* path, void* out, uint32_t length);
long __astia_fio_statat(int dirfd, const char* path, void* out, uint32_t length, int flags);
long __astia_fio_lstat(const char* path, void* out, uint32_t length);
long __astia_fio_fstat(int fd, void* out, uint32_t length);
long __astia_fio_mkdir(const char* path, int mode);
long __astia_fio_chmod(const char* path, int mode);
long __astia_fio_chown(const char* path, unsigned int uid, unsigned int gid, int nofollow);
long __astia_fio_fchown(int fd, unsigned int uid, unsigned int gid);
long __astia_fio_utime(const char* path);
long __astia_fio_unlink(const char* path, int directory);
long __astia_fio_unlinkat(int dirfd, const char* path, int directory);
long __astia_fio_rename(const char* old_path, const char* new_path);
long __astia_fio_renameat(int olddirfd, const char* old_path, int newdirfd, const char* new_path);
long __astia_fio_mkdirat(int dirfd, const char* path, int mode);
long __astia_fio_symlink(const char* target, const char* path);
long __astia_fio_readlink(const char* path, char* buffer, size_t capacity);
long __astia_fio_readlinkat(int dirfd, const char* path, char* buffer, size_t capacity);
long __astia_fio_readdir(const char* path, uint64_t index, astia_dirent* out);
long __astia_fio_ioctl(int fd, uint64_t request, void* arg, size_t length);
long __astia_fio_pipe(int fds[2]);
long __astia_fio_dup(int fd);
long __astia_fio_dup2(int old_fd, int new_fd);
void __astia_fio_after_fork(void);
#pragma GCC visibility pop
