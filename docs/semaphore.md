# Semaphore support

The libc currently implements unnamed, process-local semaphores through the
futex ABI. Named semaphore APIs (`sem_open`, `sem_close`, and `sem_unlink`)
and timed waits (`sem_timedwait`) are intentionally not exposed yet.

Implementing them correctly requires a persistent named-object namespace and
process-shared futex keys. The current kernel futex implementation keys waits
by virtual address, records the waiting process as the owner, and restricts
wakeups to that same process. A forked child therefore cannot wake a parent
waiting on the same user address, and a process-local name table would provide
incorrect cross-process behavior. The futex syscall also rejects a non-zero
timeout with `ENOSYS`, so implementing `sem_timedwait` in libc would not be
honest.

The archive split in the libc build is retained: host tests use the libc
objects without the Astia process-AIO port, while the freestanding archive
adds that port for kernel-backed userspace. Named semaphore work is blocked
until the kernel supplies a persistent semaphore namespace, shared futex
identity, and timed futex waits. The existing semaphore test deliberately
keeps `sem_init(..., pshared != 0)` as an `ENOTSUP` regression check.
