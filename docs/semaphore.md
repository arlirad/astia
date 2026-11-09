# Semaphore support

The libc implements unnamed, process-private semaphores through the futex ABI.
`sem_timedwait` accepts an absolute `CLOCK_REALTIME` deadline, returns
`ETIMEDOUT` when an unavailable semaphore reaches that deadline, and wakes
normally when another thread posts first. Invalid `tv_nsec` values return
`EINVAL`; an available semaphore is acquired without requiring deadline
validation.

The futex wait timeout is an absolute `CLOCK_MONOTONIC` `astia_timespec`.
The kernel publishes the waiter and compares timeout expiry and semaphore
wakeups under the same bucket lock, so a post racing the deadline cannot lose
the waiter. Expiration is driven by timer ticks; no userspace polling is used.

An unavailable `sem_wait` or `sem_timedwait` returns `EINTR` when a caught,
non-`SA_RESTART` signal interrupts its futex wait; a handler may post the
semaphore, leaving that post available to a subsequent wait. Unsupported cases
are named semaphores (`sem_open`, `sem_close`, and `sem_unlink`) and
process-shared semaphores (`sem_init(..., pshared != 0)` returns `ENOTSUP`).
The futex key remains process-private and address-based, so forked processes
cannot share these semaphore waits.
