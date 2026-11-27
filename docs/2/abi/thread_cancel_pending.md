# thread_cancel_pending(2)

## SYNOPSIS

```c
long astia_thread_cancel_pending(void);
```

## DESCRIPTION

Reports whether cancellation has been requested for the calling thread. The
call tests the persistent cancellation flag set by thread_cancel(2); it does
not modify the flag or perform cancellation itself.

## RETURN VALUE

Returns one when cancellation is pending, zero when it is not, or a negative
errno.

## ERRORS

- `-ESRCH`: There is no current thread.

## NOTES

The flag is not consumed by this call, so repeated calls report the same
request. Cancellation points use this predicate to decide whether to unwind
the thread.
