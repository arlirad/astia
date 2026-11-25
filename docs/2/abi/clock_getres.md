# clock_getres(2)

## SYNOPSIS

```c
long astia_clock_getres(uint32_t clock_id, astia_timespec* out);
```

## DESCRIPTION

Reads the supported resolution for a kernel clock: `CLOCK_MONOTONIC`,
`CLOCK_BOOTTIME` or `CLOCK_REALTIME`, the same set `clock_gettime` accepts. Passing a null output requests validation only.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: The output pointer is not writable when out is non-null.
- `-EINVAL`: The clock ID is unsupported.
- `-EIO`: The platform has no active timer source.

## NOTES

Resolution is derived from the active timer frequency (one tick, rounded up to
whole nanoseconds). All three clocks share the tick source, so they report the
same resolution.
