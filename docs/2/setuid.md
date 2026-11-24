# setuid(2)

## SYNOPSIS

```c
long astia_setuid(uint32_t v);
```

## DESCRIPTION

Sets the real and effective user IDs to uid. A non-root caller may select only its current real or effective UID.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EINVAL`: Uid is outside the unsigned 32-bit range.
- `-EPERM`: The current effective UID is nonzero and uid is neither the real nor effective UID.
- `-ESRCH`: There is no current process.

## NOTES

The kernel stores credentials per process.
