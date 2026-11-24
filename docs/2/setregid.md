# setregid(2)

## SYNOPSIS

```c
long astia_setregid(uint32_t r, uint32_t e);
```

## DESCRIPTION

Sets the real and effective GIDs. The raw ABI represents the libc sentinel value -1 as the unsigned all-ones value.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EINVAL`: Either argument is below the sentinel or outside the unsigned 32-bit range.
- `-EPERM`: The requested GID transition is not allowed for the current effective GID.
- `-ESRCH`: There is no current process.

## NOTES

The process implementation accepts the current real or effective GID, and root may select any 32-bit GID.
