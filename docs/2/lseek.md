# lseek(2)

## SYNOPSIS

```c
long astia_lseek(int32_t fd, int64_t offset, int32_t whence);
```

## DESCRIPTION

Changes the offset of a seekable open file description.

## RETURN VALUE

Returns the resulting file offset or a negative errno.

## ERRORS

- `-EBADF`: Fd is invalid.
- `-EINVAL`: Whence is unsupported.
- `-ESPIPE`: The open file is not seekable.

## NOTES

File descriptions shared by duplicated descriptors share the offset.
