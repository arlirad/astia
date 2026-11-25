# thread_create_owned(2)

## SYNOPSIS

```c
long astia_thread_create_owned(void (*entry)(void*, void*), void* stack_top, void* argument0, void* argument1, void* stack_addr, uint64_t stack_size);
```

## DESCRIPTION

Creates a thread using a kernel-owned stack and starts it with the requested entry and arguments.

## RETURN VALUE

Returns the new thread ID or a negative errno.

## ERRORS

- `-EFAULT`: Start is inaccessible userspace memory.
- `-EINVAL`: Entry or stack parameters are invalid.
- `-EAGAIN`: Thread resources are exhausted.
- `-ESRCH`: There is no current process.

## NOTES

The kernel owns and reclaims the allocated stack.
