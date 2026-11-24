# getrlimit(2)

## SYNOPSIS

```c
long astia_getrlimit(int32_t resource, astia_rlimit* limit);
```

## DESCRIPTION

Reads the calling process's native resource limit into output. Astia supports RLIMIT_NOFILE and RLIMIT_AS.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EFAULT`: Output is null or not writable userspace memory.
- `-EINVAL`: Resource is not supported.
- `-ESRCH`: There is no current process.

## NOTES

The limits are process-local and are inherited by fork.
