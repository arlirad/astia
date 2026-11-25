# shm_unlink(2)

## SYNOPSIS

```c
long astia_shm_unlink(const char* name);
```

## DESCRIPTION

Removes the name of a process-owned shared-memory object.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: Name is not readable userspace memory.
- `-ENOENT`: The named object does not exist.
- `-ESRCH`: There is no current process.

## NOTES

Open references remain valid after unlink.
