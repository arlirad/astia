# exit(2)

## SYNOPSIS

```c
void astia_exit(int32_t status);
```

## DESCRIPTION

Terminates the calling process and all of its threads.

The call records status as the process exit status and requests exit on every other thread in the process, waking blocked threads and aborting futex and I/O waits. The calling thread then terminates without returning. This is process-wide termination, not a single-thread exit; use thread_exit(2) to terminate only the calling thread.

The process remains registered until its last thread has terminated. It then becomes a zombie, and its parent collects the termination status with waitpid(2).

## RETURN VALUE

Does not return to the caller.

## NOTES

The first thread to request process exit supplies the status reported to the parent; later calls do not replace it. waitpid(2) reports a normal exit as (status & 0377) << 8, so only the low eight bits of status are visible to the parent. If an unhandled fatal signal terminates the process instead, the parent observes the signal rather than the recorded status.

The public wrapper astia_exit is declared ASTIA_NORETURN and reports no error. The kernel entry returns -ESRCH only when the call has no current thread context, which cannot occur for a userspace caller.

libc exit(3) runs atexit handlers and flushes streams before calling astia_exit. _Exit(3) and _exit(3) call it immediately without that cleanup.
