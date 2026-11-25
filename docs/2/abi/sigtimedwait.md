# sigtimedwait(2)

## SYNOPSIS

```c
long astia_sigtimedwait(const uint32_t* set, astia_siginfo_t* info, const astia_timespec* timeout);
```

## DESCRIPTION

Waits for one pending signal in set, optionally bounded by a relative timeout.

## RETURN VALUE

Returns the selected signal number or a negative errno.

## ERRORS

- `-EFAULT`: Info or timeout is inaccessible userspace memory.
- `-EINVAL`: Set or timeout is invalid.
- `-EAGAIN`: No selected signal arrived before timeout.
- `-EINTR`: A different signal interrupted the wait.
- `-ESRCH`: There is no current thread.

## NOTES

A null timeout waits without a deadline.
