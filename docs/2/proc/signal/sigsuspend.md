# sigsuspend(2)

## SYNOPSIS

```c
int sigsuspend(const sigset_t* mask);
```

## DESCRIPTION

Temporarily replaces the calling thread's signal mask with mask and waits for a signal. The previous mask is restored before the function returns.

## RETURN VALUE

Never returns 0. When a signal ends the wait, returns -1 and sets errno to EINTR. On failure, returns -1 and sets errno to the reported error.

## ERRORS

- `EAGAIN`: The signal waiter could not be registered.
- `EFAULT`: The mask pointer is not readable userspace memory.
- `EINTR`: A signal ended the wait; this is the normal successful return.
- `ESRCH`: There is no current process or thread.

## NOTES

The libc wrapper is implemented in [libc/src/process/signal.c](https://github.com/arlirad/astia/blob/main/libc/src/process/signal.c); the kernel wait path is [kernel/src/proc/syscalls/signal/sigsuspend.cpp](https://github.com/arlirad/astia/blob/main/kernel/src/proc/syscalls/signal/sigsuspend.cpp).
