#pragma once
#include <astia/common.hpp>
namespace astia::fs::syscall {
    /// io_enter - Submit and reap asynchronous I/O operations.
    ///
    /// Synopsis:
    ///     long io_enter(astia_rings *ring, uint32_t to_submit,
    ///                   uint32_t min_complete, uint32_t flags,
    ///                   const astia_timespec *timeout);
    ///
    /// Description:
    ///     Consumes up to to_submit published SQEs, advancing the SQ head for
    ///     each SQE dispatched, and returns the number consumed. If
    ///     min_complete is nonzero, waits until at least that many unreaped
    ///     CQEs are visible. Reaping itself is a userspace memory operation:
    ///     acquire the CQ tail, read each CQE, and advance the CQ head.
    ///     ENTER_GETEVENTS is accepted as a wait flag, and ENTER_ABS_TIMEOUT
    ///     makes a non-null timeout an absolute CLOCK_MONOTONIC deadline;
    ///     otherwise the timeout is relative. A null timeout waits without a
    ///     deadline.
    ///
    /// Return Value:
    ///     Returns the number of SQEs consumed, which can be less than
    ///     to_submit when fewer published SQEs were available. A consumed
    ///     operation may complete immediately or remain parked until its
    ///     device becomes ready. Its CQE is posted later in the latter case.
    ///     Submissions already consumed remain in effect if a completion wait
    ///     later times out.
    ///
    /// Errors:
    ///     -EFAULT: timeout is non-null but does not reference readable userspace memory.
    ///     -EINVAL: ring is not a live ring owned by the caller, flags contains an
    ///     unknown bit, or timeout has a negative field or tv_nsec outside 0 through
    ///     999999999. -EAGAIN: A completion wait was requested while interrupts were
    ///     unavailable for sleeping. -EIO: The architecture has no timer frequency with
    ///     which to convert timeout. -ETIMEDOUT: The requested minimum number of CQEs did
    ///     not become visible before the timeout expired.
    ///
    /// Notes:
    ///     ENTER_GETEVENTS does not itself specify a count; min_complete does.
    ///     A zero timeout with min_complete nonzero is a finite wait, while a
    ///     null timeout is unbounded. Timeout conversion rounds up to timer
    ///     ticks and clamps waits to one year. Parked read and write operations
    ///     retain pinned buffers and file references until the device resumes
    ///     them and their CQEs are posted.
    i64 io_enter(u64 ring, u32 submit, u32 complete, u32 flags, u64 timeout);
}
