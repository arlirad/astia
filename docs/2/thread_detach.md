# thread_detach(2)

## SYNOPSIS

```c
long astia_thread_detach(uint32_t tid);
```

## DESCRIPTION

Detach a user thread.

Marks tid detached so its resources are reclaimed after exit and it
cannot later be joined.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EINVAL`: The thread already has a joiner.
- `-ESRCH`: The thread does not exist.
