# munmap(2)

## SYNOPSIS

```c
long astia_munmap(void* addr, uint64_t len);
```

## DESCRIPTION

Remove a mapped virtual range.

Removes mappings overlapping a positive-length virtual range. The
address space may split a mapping when only part is removed.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EINVAL`: The length is not positive or the range is invalid.
- `-ESRCH`: There is no current process.
