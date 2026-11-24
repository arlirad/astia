# setegid(2)

## SYNOPSIS

```c
long astia_setegid(uint32_t v);
```

## DESCRIPTION

Sets the effective group ID to gid. A non-root caller may select only its current real or effective GID.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EINVAL`: Gid is outside the unsigned 32-bit range.
- `-EPERM`: The current effective GID is nonzero and gid is neither the real nor effective GID.
- `-ESRCH`: There is no current process.

## NOTES

Changing the effective GID does not change the real GID.
