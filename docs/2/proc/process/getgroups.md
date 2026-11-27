# getgroups(2)

## SYNOPSIS

```c
long astia_getgroups(int32_t n, uint32_t* g);
```

## DESCRIPTION

Copies up to 16 supplementary group IDs into groups. A count of zero returns the number of groups without writing groups.

## RETURN VALUE

Returns the group count, or a negative errno.

## ERRORS

- `-EINVAL`: Count is negative or greater than 16, or a nonzero Count is smaller than the number of supplementary groups.
- `-EFAULT`: Groups is not writable for the returned IDs.
- `-ESRCH`: There is no current process.

## NOTES

The count and groups pointer are interpreted independently when count is zero.
