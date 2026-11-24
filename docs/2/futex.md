# futex(2)

## SYNOPSIS

```c
long astia_futex(uint64_t* addr, uint32_t op, uint32_t val, const astia_timespec* timeout, uint32_t val2, void* addr2);
```

## DESCRIPTION

Wait on or wake a shared user word.

FUTEX_WAIT blocks while the word equals value. FUTEX_WAKE wakes up
up to value waiters. The private flag selects an address key owned
by the calling process; without it, a MAP_SHARED backing key is used.

## RETURN VALUE

Wait returns zero; wake returns the number of woken waiters.

## ERRORS

- `-EAGAIN`: The word no longer equals the expected value.
- `-EFAULT`: The futex word is not accessible userspace memory.
- `-EINVAL`: The operation is unknown.
- `-ETIMEDOUT`: The absolute CLOCK_MONOTONIC timeout elapsed.
