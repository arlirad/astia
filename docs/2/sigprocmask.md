# sigprocmask(2)

## SYNOPSIS

```c
long astia_sigprocmask(int32_t how, uint32_t set, uint32_t* old);
```

## DESCRIPTION

Block, unblock, or replace the signal mask.

## RETURN VALUE

Returns zero on success, or a negative errno.

## ERRORS

- `-EFAULT`: The old mask pointer is inaccessible.
- `-EINVAL`: The mask operation is unknown.
- `-ESRCH`: There is no current process.
