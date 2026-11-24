# sigpending(2)

## SYNOPSIS

```c
long astia_sigpending(uint32_t* set);
```

## DESCRIPTION

Copies the calling thread's pending, currently blocked signal set to set.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: Set is not writable userspace memory.
- `-ESRCH`: There is no current thread.

## NOTES

The result is a signal bit mask.
