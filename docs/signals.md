# Signal semantics

## Decision

Astia implements the classic 1..31 signal namespace only. It does **not**
provide POSIX realtime signals: there are no `SIGRTMIN`/`SIGRTMAX` values,
no `sigqueue(3)`, no `si_value` payload, and no queued multiplicity.
`sysconf(_SC_REALTIME_SIGNALS)` and `sysconf(_SC_SIGQUEUE_MAX)` both report
`-1` to make the unsupported surface discoverable.

This is a deliberate contract, not an accident of the current code. Queued
realtime signals would require widening the 32-bit signal masks and the
`astia_siginfo_t` ABI, and the current async wait path still has open
termination/cancellation work (#207, #208, #209).

## Namespace

- Signals are numbered 1..31 and `NSIG`/`signal_count` is 32.
- Signal 0 is the `kill(pid, 0)` liveness probe and never becomes pending.
- `KILL` and `STOP` cannot be caught, blocked, or ignored.
- There are no realtime numbers; `kill`/`sigaction` with 32 or above fail
  with `EINVAL`.

## Pending state and coalescing

Each process and each thread keeps its own pending bitmask and one metadata
record per signal number. A send **overwrites** the record, so:

- Repeated sends of the same signal to the same target coalesce into one
  pending signal.
- Only the most recent `siginfo_t` metadata survives; earlier payloads are
  lost. `siginfo_t` has no `si_value` field.
- Process-directed sends (`kill`) update the process pending state;
  thread-directed sends (`thread_kill`) update that thread's pending state.
  The masks are separate, so the same signal sent to both a thread and its
  process can be delivered twice. Thread-directed delivery is considered
  first.
- A send to a target whose disposition is ignore is dropped at send time,
  except `SIGCHLD` (whose pending notification parents need) and the
  uncatchable `SIGKILL`/`SIGSTOP`.
- `sigpending` returns a bitmask of `(thread pending | process pending) &
  blocked`; it reports presence, never multiplicity.

A sender cannot receive `EAGAIN` from an ordinary `kill`: sends replace
state rather than queue.

## Delivery order

- Normal delivery selects the lowest-numbered pending signal that is not
  blocked.
- `sigwaitinfo` and `sigtimedwait` select the lowest-numbered pending signal
  in the requested set and consume exactly that one. Other pending signals
  remain pending and keep their own metadata.
- A timed wait that expires returns `-1` with `errno == EAGAIN`.
- Default dispositions follow the classic table: `CHLD`, `CONT`, `URG`,
  `WINCH`, `AIO`, and `IO` ignore; `STOP`/`TSTP`/`TTIN`/`TTOU` stop; core
  faults dump; the rest terminate.

## Permissions and errors

Sending follows the existing rule: root may signal anything; otherwise the
sender's real or effective UID must match the target process's real or
effective UID. `kill` reports `-ESRCH` for a missing process, `-EPERM` for a
permission failure, and `-EINVAL` for an invalid signal number.

## Unsupported POSIX surface

- `sigqueue`, `sigval`, `si_value`, `SIGRTMIN`, and `SIGRTMAX` are not
  provided; `sysconf(_SC_SIGQUEUE_MAX)` reports `-1`.
- Programs must not rely on two sends of the same signal both being
  delivered, on FIFO ordering between sends of one number, or on carrying a
  payload.
- The LTP `sigqueue/*`, realtime `sigwaitinfo/*`, and related `sigaction`
  cases remain in `toolchain/posix-expected-failures.txt` until a queued
  model exists.
