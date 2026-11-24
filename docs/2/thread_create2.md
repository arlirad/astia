# thread_create2(2)

## SYNOPSIS

```c
long astia_thread_create2(void (*entry)(void*, void*), void* stack_top, void* argument0, void* argument1, void* stack_addr, uint64_t stack_size);
```

## DESCRIPTION

Create a user thread with two arguments.

Creates a thread and passes both arguments directly to entry on the
supplied aligned user stack.

## RETURN VALUE

Returns the new thread ID or a negative errno.

## ERRORS

- `-EAGAIN`: The thread limit or memory limit was reached.
- `-EFAULT`: The entry point, stack, or argument pointer is invalid.
- `-ESRCH`: There is no current process.
