# shm_open(2)

## SYNOPSIS

```c
long astia_shm_open(const char* name, int flags, uint32_t mode);
```

## DESCRIPTION

Opens or creates a process-owned shared-memory object identified by name.

## RETURN VALUE

Returns a descriptor or a negative errno.

## ERRORS

- `-EFAULT`: Name is not readable userspace memory.
- `-EINVAL`: Flags or mode are invalid.
- `-ENOENT`: The object does not exist and creation was not requested.
- `-ESRCH`: There is no current process.

## NOTES

The object is backed by the kernel's in-memory shared-memory namespace.
