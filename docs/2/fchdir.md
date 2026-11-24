# fchdir(2)

## SYNOPSIS

```c
long astia_fchdir(int fd);
```

## DESCRIPTION

Changes the calling process's working directory to the directory referenced by fd.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EBADF`: Fd is not an open descriptor.
- `-ENOTDIR`: Fd does not refer to a directory.
- `-ESRCH`: There is no current process.

## NOTES

The working directory is process-local and inherited by fork.
