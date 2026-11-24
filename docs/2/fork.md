# fork(2)

## SYNOPSIS

```c
long astia_fork(void);
```

## DESCRIPTION

Create a copy-on-write child process.

Creates a child sharing copy-on-write mappings with the parent.
Descriptors and the current working directory are inherited.

## RETURN VALUE

Returns the child PID in the parent and zero in the child.

## ERRORS

- `-EAGAIN`: A process, thread, mapping, or registry slot cannot be allocated.
- `-ESRCH`: There is no current user process.
