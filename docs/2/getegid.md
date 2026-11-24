# getegid(2)

## SYNOPSIS

```c
long astia_getegid(void);
```

## DESCRIPTION

Returns the calling process's effective group ID.

## RETURN VALUE

Returns the effective GID, or -ESRCH when no current process exists.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The effective GID selects group permission checks.
