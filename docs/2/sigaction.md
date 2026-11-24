# sigaction(2)

## SYNOPSIS

```c
long astia_sigaction(int32_t signal, const astia_sigaction_t* next, astia_sigaction_t* old);
```

## DESCRIPTION

Get or change a signal disposition.

## RETURN VALUE

Returns zero on success, or a negative errno.

## ERRORS

- `-EFAULT`: An action pointer or signal restorer is inaccessible.
- `-EINVAL`: The signal is invalid or cannot be caught.
- `-ESRCH`: There is no current process.
