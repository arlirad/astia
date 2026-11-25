# getcwd(2)

## SYNOPSIS

```c
long astia_getcwd(char* buffer, uint64_t length);
```

## DESCRIPTION

Copy the caller's working directory.

## RETURN VALUE

Returns the copied path length, or a negative errno.

## ERRORS

- `-EFAULT`: The output buffer is inaccessible.
- `-ERANGE`: The buffer is too small.
- `-ESRCH`: There is no current process.
