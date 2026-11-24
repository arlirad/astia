# thread_cancel_pending(2)

## SYNOPSIS

```c
long astia_thread_cancel_pending(void);
```

## DESCRIPTION

Cancels the calling thread's pending syscall continuation, if one exists.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-ESRCH`: There is no current thread.

## NOTES

Cancellation completion is one-shot; a later completion cannot replace it.
