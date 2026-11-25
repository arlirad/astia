# thread_create(2)

## SYNOPSIS

```c
long astia_thread_create(void (*entry)(void*), void* stack_top, void* argument);
```

## DESCRIPTION

Create a user thread.

Creates a thread in the current process and starts entry with
argument on the supplied aligned user stack.

## RETURN VALUE

Returns the new thread ID or a negative errno.

## ERRORS

- `-EAGAIN`: The thread limit or memory limit was reached.
- `-EFAULT`: The entry point or user stack is invalid.
- `-ESRCH`: There is no current process.
