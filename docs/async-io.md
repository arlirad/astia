# Kernel asynchronous I/O

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
