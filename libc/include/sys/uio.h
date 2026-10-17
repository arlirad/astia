#pragma once

/*
 * astia libc — <sys/uio.h>: scatter/gather reads and writes.
 *
 * readv/writev are the *position-based* pair: they move bytes at the
 * descriptor's own position and advance it by what they moved.  That is
 * the one thing the FILE layer never does — its calls name a position
 * and keep it themselves, so that seeking costs no kernel round trip —
 * which is why these two take an fd and the stream layer does not use
 * them internally.
 *
 * The descriptor comes from fileno(), or is one of the standard
 * streams': 0, 1 and 2 are what the kernel handed this process.  What a
 * mixed use costs is the stream's buffered state, exactly as POSIX
 * warns about mixing stdio with raw I/O — the bytes are still written,
 * just not in the order the program was imagining.
 *
 * Limits, both owned by the kernel rather than this layer: one call is
 * at most the port's per-call ceiling in total, and at most eight
 * entries (mm::pinned_iovec::max_segments).  A list past either limit is
 * refused whole with EINVAL, and every entry must lie in the caller's
 * address space — there is no short-call masking of a bad pointer.  A
 * short *count* still means what it means for read(2)/write(2): the
 * device ran out of bytes.
 *
 * Like every other call into the ring, not thread-safe yet.
 */

#include <sys/types.h>

#include <stddef.h>

struct iovec {
    void*  iov_base;
    size_t iov_len;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int fd, const struct iovec* iov, int iovcnt);
ssize_t writev(int fd, const struct iovec* iov, int iovcnt);

#ifdef __cplusplus
}
#endif
