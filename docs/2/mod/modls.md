# modls(2)

## SYNOPSIS

```c
long astia_modls(astia_module_record* records, uint32_t capacity, astia_module_list_info* info);
```

## DESCRIPTION

Copy a bounded snapshot of published kernel modules.

Only a process whose effective user ID is zero may call this operation. The
kernel takes one registry snapshot and then fills at most capacity records.
info always reports the number of records written in count and the total
number of published modules visible to that snapshot in total, so a caller can
first call with capacity zero and a null records pointer to learn the total.

Each record carries a stable module ID, state, flags, image size, reference
and dependency counts, and a NUL-terminated bounded name. Loading and
unloading slots are not published and never appear.

capacity is at most `ASTIA_MODULE_LIST_MAX`. A capacity larger than that is
rejected before any snapshot work.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EFAULT`: info, or records when capacity is nonzero, is null or not accessible userspace memory.
- `-EINVAL`: capacity exceeds `ASTIA_MODULE_LIST_MAX`.
- `-EPERM`: The effective UID is not zero.
- `-ESRCH`: There is no current process.

## NOTES

The snapshot is point-in-time. A later close or state change is not reflected
in records already copied out. No raw kernel pointers are exposed.
