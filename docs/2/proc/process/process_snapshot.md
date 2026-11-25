# process_snapshot(2)

## SYNOPSIS

```c
long astia_process_snapshot(astia_process_info* out, uint32_t capacity);
```

## DESCRIPTION

Copies a bounded snapshot of registered processes into output. Each record contains process IDs, credentials, state, controlling-terminal presence, and bounded names.

## RETURN VALUE

Returns the number of records copied, or a negative errno.

## ERRORS

- `-EINVAL`: Capacity exceeds ASTIA_PROCESS_SNAPSHOT_MAX, or output is null with a non-zero capacity.
- `-EFAULT`: The output range is not writable userspace memory.

## NOTES

A zero capacity returns zero without writing output. The snapshot is point-in-time and records are not handles.
