# clock_gettime(2)

## SYNOPSIS

```c
long astia_clock_gettime(uint32_t clock_id, astia_timespec* out);
```

## DESCRIPTION

Reads a kernel clock into out. CLOCK_MONOTONIC and CLOCK_BOOTTIME report uptime; CLOCK_REALTIME adds the configured epoch anchor.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: The output timespec pointer is not writable userspace memory.
- `-EINVAL`: The clock ID is unsupported.
- `-EIO`: The platform has no active timer source.

## NOTES

A platform without an active timer source returns -EIO rather than dividing by zero. Arguments enter through the direct syscall ABI.
