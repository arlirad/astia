# setrlimit(2)

## SYNOPSIS

```c
long astia_setrlimit(int32_t resource, const astia_rlimit* limit);
```

## DESCRIPTION

Changes a supported native resource limit from input. Astia supports RLIMIT_NOFILE and RLIMIT_AS.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EFAULT`: Input is null or not readable userspace memory.
- `-EINVAL`: Resource or the requested limit is invalid.
- `-EPERM`: The requested hard-limit change is not permitted.
- `-ESRCH`: There is no current process.

## NOTES

A process may lower its limits, and an unprivileged process cannot raise a hard
limit. A privileged process (effective UID 0) may raise a hard limit up to the
kernel maximum; `RLIMIT_NOFILE` remains capped by the kernel descriptor limit.
