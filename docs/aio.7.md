# Asynchronous I/O

## NAME

`core::async_operation<T>`, `fs::io_packet` - one-shot kernel I/O completion;
Astia AIO - shared submission and completion rings.

## SYNOPSIS

```cpp
core::task<i64> read(const mm::shptr<open_file>&, span<u8>, u64 offset);
core::task<i64> write(const mm::shptr<open_file>&, span<const u8>, u64 offset);
core::task<i64> read_exact(const mm::shptr<open_file>&, span<u8>, u64 offset);
```

```c
astia_sq_publish(ring, count);
astia_io_submit(ring, count);
while (astia_cq_ready(ring) != 0) {
    astia_cqe *cqe = astia_cq_peek(ring);
    handle_completion(cqe->user_data, cqe->result);
    astia_cq_consume(ring, 1);
}
```

## DESCRIPTION

Kernel asynchronous file operations submit work to the VFS without making a
device method wait. A request may complete immediately, park while the device
cannot progress, or complete from an interrupt-driven path.

`core::async_operation<T>` is a one-shot awaitable with pending, completing,
completed, and canceled states. It stores either a value or an `error_t`; one
producer wins the completion race. One awaiter attaches one continuation and
consumes the optional result without a callback or lock.

## OPERATION/DATA FLOW

`fs::io_packet` holds the open-file reference and the stable `io_request` for
one device flight. Its request owns the `async_operation<i64>` completion.
`submit()` hands the request to the file and returns an immediate submission
result. A negative result is a submission error; zero means the operation is
in flight; a positive result is an immediate byte count. A later byte count or
operation error is delivered by the completion.

Device and AIO interrupt paths use `complete_deferred()`. It publishes the
result and queues the continuation with `schedule_resume()` on the CPU that
publishes the completion. Only that CPU drains its bounded resume queue; the
scheduler drains it after a context switch, keeping arbitrary coroutines out
of interrupt handlers and preserving owner-CPU assumptions. A full queue
returns false and leaves the continuation unwoken, although the completion
result remains published.

The file helpers return coroutine tasks. `read()` and `write()` may return
short counts. `read_exact()` issues one read and returns `error_t::eio` when
that read is short; it does not loop to fill the span.

Userspace AIO represents every operation that can block with an `OP_*` SQE.
The kernel publishes a CQE, and the SQE `user_data` is copied to it so the
caller can route the result without a side table.

## LIFETIME/OWNERSHIP

The caller must keep the file reference, buffer, and returned task valid until
the operation finishes. `io_packet` owns its open-file reference and keeps its
request stable for the complete device flight. Its completion must outlive
the device's request and is consumed once.

Each ring has one submission producer and one CQ reaper. Cross-thread use
requires userspace synchronization. Registered files and buffers remain pinned
and may be selected by SQE flags; unregistered operations use ordinary
descriptor and buffer validation.

## ERRORS

`io_packet::submit()` returns `-EBADF` for a missing file and other negative
submission errors immediately. Allocation of a kernel completion can return
`error_t::enomem`. A CQE result is non-negative on success, such as a byte
count, descriptor, process ID, or readiness mask, and negative on `-errno`.
Cancellation completes the async operation with `error_t::eagain`.

A short read is a normal result for `read()` but is `error_t::eio` from
`read_exact()`. A full owner-CPU resume queue can lose the wakeup and must be
treated as a deferred-completion resource failure by the owning path.

## NOTES

`astia_io_submit()` is a submit-only kick. It submits a batch and returns
without waiting for device completion; the CQE, not the kick's return value,
is the source of truth. SQ publication uses release ordering and CQ reads use
acquire ordering. `SIGAIO` only notifies the owning process that CQEs may be
available. Drain the CQ promptly, then advance its head with release ordering;
consuming at least one CQE acknowledges the notification generation and CQ
draining requires no syscall.

## CAPACITY CONTRACT

The kernel uses fixed storage for AIO rings and parked operations. The public
header exposes the read-only limits `ASTIA_AIO_MAX_RING_SLOTS` (four rings),
`ASTIA_AIO_MAX_RING_ENTRIES` (64 SQ entries),
`ASTIA_AIO_MAX_PARKED_OPERATIONS` (64 parked operations per ring), and
`ASTIA_AIO_MAX_OPERATION_BYTES`. A fifth ring fails with `-ENFILE`; when a
ring's parked-operation queue is full, the submitted operation completes with
`-EBUSY`. These are policy and queue capacities, not CPU-topology limits, and
are not dynamically expanded.
