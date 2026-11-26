#include "file_port.h"
#include <sys/uio.h>

#include <errno.h>
#include <stdint.h>

/*
 * astia libc — <sys/uio.h> over the file port.
 *
 * The two public calls are thin: the port speaks the ABI's convention
 * (>= 0 is the byte count, < 0 one of its errno codes negated), POSIX
 * says -1 with errno, and that is the whole translation.
 *
 * FIO_OFF_APPEND is the offset that means "the descriptor's own
 * position", which is the only position a POSIX readv/writev has: it is
 * the ABI's OFF_APPEND, resolved kernel-side to the open file's
 * position.  Nothing about appending is implied by using it — the
 * stream layer passes it for a FIO_APPEND stream because that is where
 * the kernel places such writes.
 *
 * The entry bounds stay the kernel's: a vector longer than its eight
 * entries, or one whose entries do not translate in the caller's map,
 * comes back as EINVAL or EFAULT from here unchanged.  Catching either
 * in userspace would mean walking the list twice for a picture that
 * could be stale by the second walk.
 */

static ssize_t vector_call(long (*port)(int, uint64_t, const struct iovec*, uint32_t),
                           int fd, const struct iovec* iov, int iovcnt) {
    long rc;

    if (iovcnt < 0 || (uintmax_t) iovcnt > UINT32_MAX) {
        errno = EINVAL;

        return -1;
    }

    rc = port(fd, FIO_OFF_APPEND, iov, (uint32_t) iovcnt);
    if (rc < 0) {
        errno = (int) -rc;

        return -1;
    }

    return (ssize_t) rc;
}

ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    return vector_call(__astia_fio_readv, fd, iov, iovcnt);
}

ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    return vector_call(__astia_fio_writev, fd, iov, iovcnt);
}
