# thread_getattr(2)

## SYNOPSIS

```c
long astia_thread_getattr(uint32_t tid, astia_thread_stack_info* output);
```

## DESCRIPTION

Reads attributes for a thread in the calling process.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: Out is not writable userspace memory.
- `-ESRCH`: The thread does not exist.

## NOTES

Returned stack details describe kernel-managed thread state.
