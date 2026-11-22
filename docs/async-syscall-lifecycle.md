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
- `active_calls_` is one atomic word holding the call count, a delete-armed bit,
  and a deleting sentinel. `leave_call()` claims the final deletion with the
  same CAS that decrements the last call, so a concurrent reaper can never free
  the object between decrement and deletion. `enter_call()` rejects both the
  armed and deleting states.

## Remaining limitations

- Completion awaiters in coroutines that are not `core::task` frames are not
  protected by task-frame quiescence; production async paths use `core::task`.
- The lifecycle table and operation registry are fixed-capacity. Exhaustion
  suppresses resume or leaks a deferred operation rather than forking an
  unsafe fallback.
- Cancellation tears down the coroutine frame; it does not cancel arbitrary
  device or filesystem work behind a handler. Handlers must not keep
  references into their own coroutine frame.

## Structural closure

- `completion` tracks attached awaiters (`awaiter_refs_`) and detach callbacks in
  flight (`detach_inflight_`). A wake that took the continuation cannot let the
  completion/operation be freed before the awaiter detaches. Deferred wake
  publication holds `deferred_inflight_`; `detach()` waits for publication and
  then cancels the queued slot by completion key, not just generation.
- Resume queue slots carry a unique completion key. Duplicate detection and
  cancellation match handle + generation + key, so two distinct completions
  awaiting the same handle/generation cannot swallow each other's wake.
- `drain_resumes()` keeps the lifecycle reference through `resume()`,
  `await_resume`, detach, and `resume_end`.
- `async_operation` has a `create_ref_` held until the creator calls
  `destroy()`. Owner exit can cancel and tear down the driver but cannot reap
  the operation while the creator still holds the returned pointer. This
  closes the create/owner-exit publication window.
- `driver_state_` is one atomic handshake: `idle → starting → started`, with
  `teardown_requested → teardown_running → destroyed` arbitration. A teardown
  request that races a start cannot be lost between separate flags; exactly one
  side claims `teardown_running` and resets the driver. This closes the
  start/begin-teardown A < X < Y < B lost-teardown interleaving.
- Registry scans hold a registry reference while invoking the exit hook.
  `maybe_reap()` refuses to queue while `registry_refs_` is nonzero, and the
  registry invokes an operation refs-zero callback after the hook releases the
  last reference. That callback unregisters the operation under the registry
  lock before attempting reaping, so no new scan can acquire a reference and a
  reaper cannot delete while the zero callback is still running. The callback
  holds an operation active call across `maybe_reap()`. The registry also sets
  an operation zero-inflight guard before the final ref drop; `maybe_reap()`
  and the reaper refuse deletion while that guard is set, and the zero callback
  clears it only after unregistering. Reaper deletion rechecks both registry
  refs and the zero guard, so a cancel hook can never destruct the operation
  while its own reference or zero callback is live. `retire_operation()` is the
  authoritative deletion gate: it runs under the registry lock, verifies refs
  and zero guard there, and removes the entry before any reaper arm/delete, so
  no scan can acquire a ref after the deletion decision.
- The handler body frame has its own destroy observer and operation gate.
  Reaping requires both the driver frame and the body frame to be confirmed
  destroyed. A deferred body resume therefore keeps `async_operation` and its
  owned `syscall_context` alive until the body frame actually leaves the
  resume path. `destroy_coroutine()` invokes the observer after immediate
  destruction as well as after deferred destruction.
- `owner_exit()` publishes terminal `eagain` to any attached result awaiter,
  then tears down the driver. Reaping waits for awaiter detach and detach
  callback completion.
- `maybe_reap()` waits for `driver_destroyed_`, `create_ref_ == 0`,
  `awaiter_count() == 0`, and `detach_inflight() == false`. If the bounded
  reaper is full, it falls back to the idle-delete gate rather than dropping
  the object. Exhausting the lifecycle table is explicit: resume/destroy
  returns false instead of an unguarded frame access.
- `resume_end()` releases zero-reference lifecycle slots, so transient wake
  refs cannot permanently exhaust the table.

## Tests

Host tests cover inline wake, deferred wake, queue-overflow fallback, detach
vs destroy quiescence, reentrant inline completion, owner registration and
unregistration, process/thread exit cancellation, exactly-once terminal
result, cancellation during wake, destruction while parked, and reaper
draining.
