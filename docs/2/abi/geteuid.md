# geteuid(2)

## SYNOPSIS

```c
long astia_geteuid(void);
```

## DESCRIPTION

Returns the calling process's effective user ID.

## RETURN VALUE

Returns the effective UID, or -ESRCH when no current process exists.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The effective UID selects owner permission checks.
