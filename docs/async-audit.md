# Async audit

Audit target: `5a3a317` (focused async/coroutine/AIO paths). The deferred
continuation, stale-handle, CQ-full completion, and parked filesystem lifetime
findings below are fixed by the async slices.

## Confirmed findings

### Fixed — deferred wake drops a live continuation

`kernel/include/astia/core/async_operation.hpp:40-45` exchanges the waiting
coroutine handle before calling `schedule_resume()`, then discards the boolean
return value. `kernel/src/core/resume_queue.cpp:11-16` uses a fixed 256-entry
queue and returns `false` when the per-CPU queue is full (or the CPU index is
invalid). There is no retry, fallback, or cancellation of the extracted
handle. The operation is nevertheless published completed at
`async_operation.hpp:118-134`.

Focused probe `/tmp/opencode/deferred_drop_probe.cpp` supplied a
`schedule_resume()` implementation that always returned false. It observed:
`complete=1 op_done=1 task_done=0 resumed=0`.
`/tmp/opencode/resume_queue_capacity_probe.cpp` independently filled the real
`mpmc_queue<coroutine_handle<>, 256>`: 256 pushes succeeded and the 257th
returned false. Existing focused CTest tests passed, but none exercise this
failure path.

### Fixed — queued continuation can outlive a destroyed task frame

`async_operation::wake_deferred()` removes the handle from the operation and
publishes it to the resume queue (`async_operation.hpp:40-45`). `task::reset()`
then destroys the coroutine frame without removing handles already queued
(`task.hpp:246-251` and `370-375`). `drain_resumes()` unconditionally resumes
each queued handle (`resume_queue.cpp:18-27`). Therefore completion followed by
task destruction before queue drain leaves a stale handle in the queue; a later
drain resumes a destroyed frame. The same race exists between
`async_operation::wake()`'s atomic exchange (`async_operation.hpp:33-38`) and
awaiter destruction (`async_operation.hpp:61-64, 181-187`): if wake exchanges
the handle and is preempted before `resume()`, destruction can destroy the
frame and wake then resumes it.

The deterministic probe `/tmp/opencode/deferred_destroy_probe.cpp` captured a
queued handle, reset the task, and then invoked the queued handle. It completed
without an ASan diagnostic, but that invocation is still unconditional use of
a destroyed `std::coroutine_handle`; the source-level interleaving is
deterministic and the behavior is undefined.

### Fixed — AIO can consume an inline SQE while losing a full-CQ completion

`kernel/src/sys/aio.cpp` now stores the result in the fixed park slot when
`cq_post()` reports a full CQ. The SQE is consumed exactly once, but the slot
stays active until `io_submit(ring, 0)` retries publication successfully.

### Fixed — parked AIO completion can remain permanently stuck after CQ full

`resume_parked()` now stores the completed result instead of re-pumping the
device. `io_submit(ring, 0)` retries all pending CQEs after userspace advances
the CQ head, preserving one CQE and one device submission.

### Fixed — parked `fs_async` request has no destruction cancellation

`io_packet` now owns an embedded `io::operation` with explicit device retain,
cancel, active-use, and acknowledgement states. Task destruction cancels the
flight and releases only the owner reference; a parked device must
acknowledge before request/completion storage is reclaimed.

## Call-site results

- `async_operation::complete()/fail()/cancel()` use inline `wake()` by design
  (`async_operation.hpp:82-116`) and are appropriate only for task context.
- No production callsite invokes those methods from an IRQ/device-ready path.
  The IRQ-ready path is `io_device_ready_context()` → `resume_parked()` and
  its async-operation branches use `complete_deferred()/fail_deferred()` at
  `aio.cpp:819-840`.
- No direct coroutine `.resume()` appears in the IRQ-ready path. The only
  resume in `resume_queue.cpp` is scheduler drain context.
- `grep` found async-operation allocation only in `fs_io.cpp`; current AIO
  `parked_op` requests are constructed without an async-operation completion,
  so the deferred-completion branches in AIO are defensive/future-facing.

## Verification

Focused CTest run passed:

```text
ctest --test-dir /tmp/opencode/astia-kernel-tests --output-on-failure \
  -R 'astia_(task|fs_async|mpmc_queue|aio)_test'
```

Result: focused async tests passed, including 300 deferred completions,
completion-before-destroy, inline filesystem completion, parked completion, and
completion after task cancellation. The kernel integration selftest covers
full-CQ inline and parked completions, and full kernel CTest passed 32/32.
