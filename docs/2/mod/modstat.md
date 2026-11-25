# modstat(2)

## SYNOPSIS

```c
long astia_modstat(const astia_module_id* id, astia_module_stat* out);
```

## DESCRIPTION

Copy one stable snapshot of a published kernel module.

Only a process whose effective user ID is zero may call this operation. The
kernel resolves id in one registry snapshot and copies the module's state,
flags, image size, active-reference count, dependent count, and bounded name
into out.

state is loaded, running, or exiting. flags report running, initialized,
resident, quiescing, and unloadable status. Resident modules and modules with
active references or dependents are not unloadable.

The returned dependency_count is the total stored edge count;
dependencies_written is the number of dependency records copied into the
bounded inline array. Each dependency record carries the provider's stable
module ID.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EBUSY`: The selected slot is loading or unloading and is not published.
- `-EFAULT`: id or out is null or not accessible userspace memory.
- `-EINVAL`: id carries a nonzero reserved field.
- `-ENOENT`: The ID is invalid, stale, or names no module.
- `-EPERM`: The effective UID is not zero.
- `-ESRCH`: There is no current process.

## NOTES

The snapshot is point-in-time. No raw kernel pointers are exposed.
