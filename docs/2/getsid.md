# getsid(2)

## SYNOPSIS

```c
long astia_getsid(int32_t pid);
```

## DESCRIPTION

Returns the session ID of the selected process. Zero selects the caller; a positive PID selects that process.

## RETURN VALUE

Returns the session ID, or a negative errno.

## ERRORS

- `-ESRCH`: The selector is negative, overflows the PID domain, names no registered process, or there is no current process.

## NOTES

Zombie processes remain selectable until reaped. The native query does not impose an additional permission check.
