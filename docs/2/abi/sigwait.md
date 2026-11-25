# sigwait(2)

## SYNOPSIS

```c
long astia_sigwait(const uint32_t* set, int32_t* signal);
```

## DESCRIPTION

Waits for one pending signal in set and stores its number in signal.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: Signal is not writable userspace memory.
- `-EINVAL`: Set is invalid or contains an unblocked signal.
- `-EINTR`: The wait was interrupted by a different signal.
- `-ESRCH`: There is no current thread.

## NOTES

Signals selected by set should be blocked before entering the wait.
