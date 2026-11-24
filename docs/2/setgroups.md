# setgroups(2)

## SYNOPSIS

```c
long astia_setgroups(int32_t n, const uint32_t* g);
```

## DESCRIPTION

Replaces the supplementary group list with count IDs copied from groups.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EINVAL`: Count is negative or greater than 16.
- `-EFAULT`: Groups is not readable for count IDs.
- `-EPERM`: The current effective GID is nonzero.
- `-ESRCH`: There is no current process.

## NOTES

A zero count is valid and does not dereference groups.
