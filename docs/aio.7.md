# Asynchronous I/O

Kernel asynchronous file operations connect coroutine tasks to the VFS without
making a device method wait. A file operation is submitted, parked when the
device cannot make progress, and completed when the device reports readiness.

## Completion and `io_packet`

`core::async_operation<T>` is a one-shot awaitable. It has an atomic pending,
completing, completed, or canceled state and stores either a value or an
`error_t`. Exactly one producer wins completion. The awaiter attaches one
continuation and consumes the optional result after completion; it does not
need a callback or a lock.

`fs::io_packet` owns the open-file reference and the `io_request` for one
in-flight operation. Its request remains stable for the complete device flight
and carries the `async_operation<i64>` completion. `submit()` only hands the
request to the file; it returns an immediate submission error such as
`-EBADF`, while the byte count or operation error arrives through the
completion.

## Owner-CPU resumption

An operation completed in ordinary task context may resume its continuation
directly. An interrupt path must not run an arbitrary coroutine while the IRQ
is active. Device and AIO IRQ paths therefore use deferred completion:
`complete_deferred()` publishes the result and places the continuation in the
current CPU's resume queue.

`schedule_resume()` selects the CPU that publishes the completion. Only that
CPU drains its queue, from the scheduler after a context switch. This keeps a
coroutine's owner-CPU assumptions intact and keeps interrupt handlers bounded;
the queue is bounded, so failure to enqueue must be handled as a lost wakeup or
resource error by the owning operation.

## Async file APIs

The kernel file helpers are coroutine functions:

```cpp
core::task<i64> read(const mm::shptr<open_file>& file,
                     span<u8> buffer, u64 offset = offset_current);
core::task<i64> write(const mm::shptr<open_file>& file,
                      span<const u8> buffer,
                      u64 offset = offset_current);
core::task<i64> read_exact(const mm::shptr<open_file>& file,
                           span<u8> buffer,
                           u64 offset = offset_current);
```

The caller keeps the file reference and buffer valid until the returned task
finishes. `read()` and `write()` may return short counts. `read_exact()` keeps
issuing reads until the requested span is filled or returns an error or
short-read result.

## Userspace AIO rings

Astia's userspace I/O ABI is a shared submission/completion ring. Operations
that can block are represented by `OP_*` submission queue entries (SQEs), and
the kernel publishes completion queue entries (CQEs). The ring is single
producer and single reaper unless userspace adds its own synchronization.

## Submit-only kick

Userspace fills an SQE completely, publishes the SQ tail with release ordering,
and calls `astia_io_submit()` to kick the kernel. The kick submits a batch and
returns immediately; it is not a wait-for-completion operation. A device may
park the request, so the return value of the kick is not the operation result.
The CQE, whose `user_data` echoes the SQE tag, is the source of truth.

```c
astia_sq_publish(ring, count);
astia_io_submit(ring, count);

while (astia_cq_ready(ring) != 0) {
    astia_cqe *cqe = astia_cq_peek(ring);
    handle_completion(cqe->user_data, cqe->result);
    astia_cq_consume(ring, 1);
}
```

Successful CQE results are non-negative payloads such as bytes, a descriptor,
or a process ID. Negative results are `-errno`. SQE links and drains order
submissions, but they do not turn the submit syscall into a blocking call.

## `SIGAIO` and CQ draining

When CQEs become available, the owning process may receive `SIGAIO`. The signal
is a notification, not a completion payload and not a replacement for draining
the CQ. A handler or resumed event loop reads CQEs with acquire ordering until
the queue is empty, then advances the CQ head with release ordering. Consuming
at least one CQE acknowledges the ring's notification generation.

CQ draining costs no syscall. Userspace must drain promptly so the kernel can
reuse CQ slots, and must not have two independent reapers advance the same CQ
head. Registered files and buffers can be pinned once and selected by SQE
flags; otherwise each operation uses its ordinary descriptor and buffer
validation path.
