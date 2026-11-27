# thread_join(2)

## SYNOPSIS

```c
long astia_thread_join(uint32_t tid, void** result);
```

## DESCRIPTION

Wait for and reap a user thread.

Waits for tid to terminate and optionally copies its result pointer
to result. Only one joiner is allowed.

## RETURN VALUE

Returns zero after reaping the thread or a negative errno.

## ERRORS

- `-EFAULT`: The result pointer is not writable userspace memory.
- `-EDEADLK`: The caller attempted to join itself.
- `-EINVAL`: The thread is detached or already joined.
- `-ESRCH`: The thread does not exist.
