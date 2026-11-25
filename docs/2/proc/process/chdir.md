# chdir(2)

## SYNOPSIS

```c
long astia_chdir(const char* path);
```

## DESCRIPTION

Change the caller's working directory.

## RETURN VALUE

Returns zero on success, or a negative errno.

## ERRORS

- `-EFAULT`: The path pointer is inaccessible.
- `-ENOTDIR`: The path does not identify a directory.
- `-ENOENT`: The directory does not exist.
- `-ESRCH`: There is no current process.
