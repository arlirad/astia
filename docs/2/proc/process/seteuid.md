# seteuid(2)

## SYNOPSIS

```c
long astia_seteuid(uint32_t v);
```

## DESCRIPTION

Sets the effective user ID to uid. A non-root caller may select only its current real or effective UID.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EINVAL`: Uid is outside the unsigned 32-bit range.
- `-EPERM`: The current effective UID is nonzero and uid is neither the real nor effective UID.
- `-ESRCH`: There is no current process.

## NOTES

Changing the effective UID does not change the real UID.
