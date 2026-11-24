# msync(2)

## SYNOPSIS

```c
long astia_msync(void* addr, uint64_t len, uint32_t flags);
```

## DESCRIPTION

Validates a mapped range and synchronization flags. The current in-memory mapping implementation has no backing-store flush operation.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EINVAL`: The range or flags are invalid.
- `-EFAULT`: The requested range is not mapped.
- `-ESRCH`: There is no current process.

## NOTES

No data is persisted to a host or disk filesystem.
