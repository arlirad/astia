# sigaltstack(2)

## SYNOPSIS

```c
long astia_sigaltstack(const astia_stack_t* stack, astia_stack_t* old_stack);
```

## DESCRIPTION

Reads or changes the calling thread's alternate signal stack.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EFAULT`: Next or old points to inaccessible userspace memory.
- `-EINVAL`: The stack range or flags are invalid.
- `-ESRCH`: There is no current thread.

## NOTES

The alternate stack is per-thread.
