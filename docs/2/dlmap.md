# dlmap(2)

## SYNOPSIS

```c
long astia_dlmap(int32_t fd, uint32_t flags, astia_dl_info* info);
```

## DESCRIPTION

Map a shared-library ELF image into the calling process.

The file open at fd must be an ELF shared object of the architecture in use
(`ET_DYN`). The kernel places the image at a process-independent base,
shares the read-only segments physically with every other process that
maps the same file, and maps writable segments copy-on-write. The same
file mapped twice in one process returns the same base and just adds a
handle.

Identity is the file, not the path: two descriptors for the same inode
share one image, and the kernel keeps the descriptor's file alive for as
long as any process maps it.

On success the load bias is returned and info is filled in. Every
segment sits at `base + p_vaddr`; `phdr` is the in-memory address of the
program header table, so a userspace linker can walk `PT_DYNAMIC`,
`PT_TLS` and the relocation tables itself.

## RETURN VALUE

Returns the load bias (the handle dlunmap takes) or a negative errno.

## ERRORS

- `-EACCES`: The image is not readable by the caller.
- `-EAGAIN`: Another dlmap of a different image is still loading; retry.
- `-EBADF`: fd is not an open descriptor.
- `-EFAULT`: info is null or not writable userspace memory.
- `-EINVAL`: flags is not 0, or the file is not a loadable ELF image.
- `-ENOMEM`: The image or its address space could not be allocated.
- `-ENOSPC`: The process already holds every library binding it can.
- `-ESRCH`: There is no current process.
