# getgid(2)

## SYNOPSIS

```c
long astia_getgid(void);
```

## DESCRIPTION

Returns the calling process's real group ID.

## RETURN VALUE

Returns the real GID, or -ESRCH when no current process exists.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The value is inherited across fork and spawn.
