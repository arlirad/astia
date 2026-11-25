# setsid(2)

## SYNOPSIS

```c
long astia_setsid(void);
```

## DESCRIPTION

Create a new session for the caller.

## RETURN VALUE

Returns the new session ID, or a negative errno.

## ERRORS

- `-EPERM`: The caller is already a process-group leader.
- `-ESRCH`: There is no current process.
