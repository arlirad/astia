# Kernel synchronization contract

This is the lock contract for kernel code.  It describes the intended
*partial* hierarchy; unrelated locks must not be nested merely to make a
total order.  A caller must acquire a stable reference, copy the state it
needs, drop the lock, and then call across a subsystem boundary.

## Interrupts and lock ordering

* The maskable-interrupt gate is the outermost guard for any lock that an IRQ
  or interrupt epilogue can take.  Use `arch::interrupt_spinlock_guard`: it
  masks before acquiring the `core::spinlock` and restores the previous state
  only after unlocking.  `core::spinlock_guard` does not mask interrupts.
  Never sleep, halt, or run a callback with the gate masked.
* The process registry protects membership and PID/group lookup.  It may be
  used to obtain a process reference, but registry code must not call process,
  fd, mmap, VFS, scheduler, or signal operations while `registry_lock` is
  held.
* A process's lifecycle/thread lock and its wait-list lock protect separate
  process-local state.  The wait-list lock covers only waitpid/signal waiter
  registration and extraction; wakeups run after it is released.  The
  lifecycle lock covers process state and the thread table.  Neither precedes
  the descriptor table or mmap list: code must not hold one while entering
  the other.  The fd-table lock protects descriptor slots only; the mmap lock
  protects VMA/list state only.  Copy a descriptor or VMA reference under its
  lock, then operate on the referenced object after unlocking.
* Scheduler CPU-queue locks are independent process locks.  When two queues
  are needed, acquire them in ascending CPU-index order and release them in
  reverse order.  Do not invoke process, wakeup, device, or scheduler policy
  callbacks while holding a queue lock.
* VFS mount-registry lock precedes a mount-operation reference, but not
  filesystem object/file locks.  A mount operation increments its activity
  count under the registry lock, releases it, and calls the mount outside the
  lock.  A filesystem implementation may then take its namespace lock before
  its file lock; it must not reverse that order.
* A socket endpoint lock may precede its channel lock.  For operations
  involving two endpoints, never hold one endpoint lock while acquiring the
  other; snapshot the peer/reference first.  Channel locks are acquired in
  the channel's own scope only.
* AIO's ring-pool lock precedes a per-ring lock only for lookup/lifetime
  pinning.  Release the pool lock once the ring lock pins the slot; never hold
  both during dispatch, mapping/unmapping, reclamation, sleep, or completion
  notification.  Ring/device readiness may re-enter the AIO engine.

These rules intentionally leave the registry, process, fd, mmap, scheduler,
VFS, socket, and AIO families unordered with respect to one another.  If a
new path needs more than one family, take a reference or snapshot, release the
first lock, and acquire the next family independently.  If that is impossible,
document the new edge and audit all callers before adding it.

## No callbacks under locks

No lock may be held while calling code that can run arbitrary or re-entrant
kernel work: filesystem or file methods, device readiness/AIO notification,
wakeups or signal delivery, scheduler entry points, mapping teardown, memory
object destruction, or user-copy/error callbacks.  Queue work for after the
critical section, or move owned references into a local list and let their
destructors run after unlocking.  This rule also applies to callbacks that are
currently synchronous: an IRQ, completion, or future implementation may
re-enter the caller.
