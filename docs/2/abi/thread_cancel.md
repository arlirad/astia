# thread_cancel(2)

## SYNOPSIS

```c
long astia_thread_cancel(uint32_t tid);
```

## DESCRIPTION

Requests cancellation of a thread owned by the calling process.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EINVAL`: Tid selects the caller.
- `-ESRCH`: The thread does not exist in the process.

## NOTES

Cancellation is cooperative with the target's syscall continuation.
