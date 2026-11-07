# Semaphore support

The libc currently implements unnamed, process-local semaphores through the
futex ABI. Named semaphore APIs (`sem_open`, `sem_close`, and `sem_unlink`)
are intentionally not exposed yet.

Implementing them correctly requires a persistent named-object namespace and
process-shared futex keys. The current kernel futex implementation keys waits
by virtual address and restricts wakeups to the owning process, while
`sem_init` deliberately rejects `pshared != 0`. A process-local name table
would therefore provide incorrect cross-process behavior, so these APIs are
deferred until the kernel primitives exist.
