# thread_cancel(2)

## SYNOPSIS

```c
long astia_thread_cancel(uint32_t tid);
```

## DESCRIPTION

Requests cancellation of the thread identified by `tid` in the calling
process. The calling thread may cancel itself.

The request sets the target's persistent cancellation flag. If the target is
blocked in a wait, it is woken so it can observe the request. Cancellation is
cooperative: the target acts on the request at its next cancellation point or
syscall continuation, and a blocked syscall may return `-EINTR`.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-ESRCH`: There is no current process, or `tid` does not identify a live
  thread in the process.

## NOTES

Repeated requests do not accumulate; the cancellation flag stays set. Use
thread_cancel_pending(2) to test whether the calling thread's request is
outstanding.
