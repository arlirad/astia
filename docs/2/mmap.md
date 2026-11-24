# mmap(2)

## SYNOPSIS

```c
long astia_mmap(void* addr, uint64_t len, uint32_t prot, uint32_t flags, int32_t fd, int64_t off);
```

## DESCRIPTION

Map anonymous or file-backed memory.

Maps a positive-length range into the calling process. Anonymous
mappings require fd -1 and offset 0; file mappings require a valid
descriptor and non-negative offset.

## RETURN VALUE

Returns the mapped base address, or a negative errno.

## ERRORS

- `-EBADF`: The file descriptor is invalid for a file mapping.
- `-EINVAL`: The length, offset, protection, flags, or anonymous sentinel is invalid.
- `-ENOMEM`: The address space or physical backing cannot be allocated.
- `-ENOSYS`: The requested mapping mode is not implemented.
