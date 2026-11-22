# Async syscall lifecycle

Status: generic lifecycle runtime. No syscall, including `execve`, is converted
to it. Canonical sync and async syscall modes, descriptors, and production
dispatch are unchanged.

## Frame quiescence

`core::coroutine_lifecycle` owns a bounded table keyed by coroutine handle. A
producer takes a resume reference before it touches a continuation, and
releases it only after `handle.resume()` returns.

- Inline `completion::wake()` takes the reference before the continuation CAS,
  resumes inline, then releases it.
- Deferred completion transfers that reference to the scheduler resume slot.
  `schedule_resume()` takes the reference; `cancel_resume()` releases a queued
  slot; `drain_resumes()` resumes and releases a running slot.
- Queue overflow falls back to an acknowledged inline resume, still holding the
  reference.
- `core::destroy_coroutine()` destroys immediately when the reference count is
  zero. Otherwise it marks a pending destroy and returns; the final
  `resume_end()` destroys the frame and invokes the optional destroy observer.
- `core::task::reset_with_observer()` routes typed and void task frames through
  this protocol. A frame can no longer be freed between a producer's
  continuation exchange and its resume.

The table is bounded by `resource_limits::coroutine::lifecycle_slots`. If it is
exhausted, resume is suppressed rather than performed without an
acknowledgement. Callers must not rely on progress beyond that bound.

## Operation lifetime

`sys::async_operation` owns a copied `arch::syscall_frame`, an owned
`syscall_context`, the ABI registers, `operation_id`, `syscall_user_data`, the
handler body task, and a driver task.

- `create()` heap-allocates the operation and registers it with
  `core::operation_registry` when an owner is present.
- Process and thread records carry monotonic lifecycle IDs from
  `core::allocate_lifecycle_id()`.
- `process::request_exit()` cancels every operation registered to the process.
- `process::thread_exited()` cancels operations registered to the exiting
  thread.
- Registry scans take a registry reference on the operation before releasing
  the registry lock. The operation destructor unregisters and waits for those
  references, so a hook cannot race object destruction.
- A process/thread exit hook only terminalizes cancellation and tears down the
  driver through the quiescence protocol. It never waits for handler code.
- `core::reap_operations()` is called by the scheduler after a context switch.
  Deferred operation destruction is queued there and runs only after the
  driver frame has been destroyed.

## State machine

| State | Meaning |
| --- | --- |
| `created` | Driver and body are allocated but not resumed. |
| `running` | `start()` is executing the driver. |
| `parked` | The body suspended; userspace must not be returned to. |
| `canceled` | Terminal cancellation. Result delivery may have been suppressed. |
| `completed` | Terminal success or error. |

`start()` returns `completed`, `parked`, or `canceled`. `parked` is the explicit
contract that the syscall entry must block the calling thread instead of
returning to userspace.

`finish()` and `terminalize()` serialize on the operation lock. Exactly one
moves a non-terminal operation terminal. Later attempts return `false`.

## Cancellation semantics

Cancellation is terminal for result delivery, not a forced interruption of
handler code:

- `request_cancel()` publishes `eagain` through the owner completion and tears
  down the driver at quiescence.
- Owner exit (process/thread) terminalizes cancellation without publishing a
  result to a dying userspace owner, tears down the driver at quiescence, and
  hands the operation object to the bounded reaper.
- If a producer is inside `resume()` when teardown is requested, frame
  destruction is deferred until that resume returns. The body may run one
  final step, but it cannot be resumed after the frame is destroyed.
- `destroy_claimed_` is a compare-and-exchange ownership token: owner release
  and owner-exit cancellation cannot both queue or delete the same operation.
- `teardown_state_` tracks driver teardown independently of `driver_` handle
  ownership. `destroy()` never frees the operation when teardown was deferred;
  only `frame_destroyed_trampoline` plus the reaper can do that.
- `active_calls_` plus `delete_when_idle_` keep `request_cancel()`, `shutdown()`,
  `owner_exit()`, and `destroy()` from freeing an operation while another call
  is still using it.

## Remaining limitations

- Completion awaiters in coroutines that are not `core::task` frames are not
  protected by task-frame quiescence; production async paths use `core::task`.
- The lifecycle table and operation registry are fixed-capacity. Exhaustion
  suppresses resume or leaks a deferred operation rather than forking an
  unsafe fallback.
- Cancellation tears down the coroutine frame; it does not cancel arbitrary
  device or filesystem work behind a handler. Handlers must not keep
  references into their own coroutine frame.

## Tests

Host tests cover inline wake, deferred wake, queue-overflow fallback, detach
vs destroy quiescence, reentrant inline completion, owner registration and
unregistration, process/thread exit cancellation, exactly-once terminal
result, cancellation during wake, destruction while parked, and reaper
draining.
