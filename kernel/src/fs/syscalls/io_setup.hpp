#pragma once

#include <astia/common.hpp>

namespace astia::fs::syscall {
    /// io_setup - Create an asynchronous I/O ring.
    ///
    /// Synopsis:
    ///     long io_setup(uint32_t entries, uint32_t flags, astia_rings **out);
    ///
    /// Description:
    ///     Allocates a ring owned by the calling process and stores its userspace
    ///     address in out. The requested submission capacity is rounded up to a
    ///     power of two. The current implementation accepts 1 through 64
    ///     entries and requires flags to be zero. The completion ring has
    ///     max(64, 2 * sq_entries) entries.
    ///
    /// Return Value:
    ///     Returns zero and initializes *out on success. The returned ring
    ///     contains one shared submission and completion mapping. The SQ is
    ///     submitted by publishing its tail and calling io_enter; CQEs are
    ///     reaped by userspace by advancing the CQ head, without a syscall.
    ///
    /// Errors:
    ///     -EFAULT: The out pointer does not reference writable userspace memory.
    ///     -EINVAL: entries is zero, entries is greater than 64, or flags contains a bit other than zero.
    ///     -ENOMEM: The kernel cannot create the userspace alias for the ring mapping.
    ///     -ESRCH: The caller has no address space in which the ring alias can be mapped.
    ///     -ENFILE: All four kernel ring slots are already in use.
    ///
    /// Notes:
    ///     Userspace owns the SQ tail and CQ head; the kernel owns the SQ head
    ///     and CQ tail. One SQ producer and one CQ consumer are supported per
    ///     ring, so callers must serialize additional threads externally. The
    ///     current implementation reports FEAT_SINGLE_MMAP only; SETUP_SQPOLL
    ///     and SETUP_IOPOLL are not accepted.
    ///
    i64 io_setup(u32 entries, u32 flags, u64 out);
}
