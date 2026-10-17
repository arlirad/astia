#pragma once

/*
 * src/file_port.h — the file I/O port (private to the libc).
 *
 * Seven primitives: one per blocking file operation of the userspace
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

long __astia_fio_open(const char* path, int flags, int mode);
long __astia_fio_read(int fd, uint64_t off, void* buf, size_t n);
long __astia_fio_write(int fd, uint64_t off, const void* buf, size_t n);
long __astia_fio_readv(int fd, uint64_t off, const struct iovec* iov, uint32_t count);
long __astia_fio_writev(int fd, uint64_t off, const struct iovec* iov, uint32_t count);
long __astia_fio_close(int fd);
long __astia_fio_size(int fd, uint64_t* out); /* file length, for SEEK_END */
