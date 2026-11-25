# getuid(2)

## SYNOPSIS

```c
long astia_getuid(void);
```

## DESCRIPTION

Returns the calling process's real user ID.

## RETURN VALUE

Returns the real UID, or -ESRCH when no current process exists.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The value is the process credential inherited across fork and spawn.
