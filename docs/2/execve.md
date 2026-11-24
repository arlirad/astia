# execve(2)

## SYNOPSIS

```c
int execve(const char* path, char* const argv[], char* const envp[]);
```

## DESCRIPTION

Replaces the calling process image with the executable at path. The process ID, process group, and working directory remain associated with the process. Descriptors without FD_CLOEXEC remain open; descriptors marked FD_CLOEXEC are closed. The argument and environment vectors are copied before the image is replaced.

## RETURN VALUE

Does not return on success. On failure, returns -1 and sets errno.

## ERRORS

- `E2BIG`: The argument or environment vector exceeds its limit.
- `EFAULT`: An argv or envp vector or string pointer is not readable userspace memory. libc resolves path before the syscall, so a null path returns EINVAL and an invalid non-null path pointer can fault in libc instead of returning EFAULT.
- `EINVAL`: The path is null, or the file is nonempty but is not a valid ELF executable image (malformed header, unsupported executable type, or invalid segment metadata).
- `ENAMETOOLONG`: The resolved path exceeds the libc path buffer.
- `ENOENT`: The executable path does not exist or cannot be opened. The kernel also returns this when there is no current process or thread context.
- `ENOEXEC`: The file is empty.
- `ENOMEM`: The replacement image cannot be allocated.
- `ESRCH`: The current process or thread could not be retained for image replacement.

## NOTES

Relative paths are resolved against the caller's working directory by libc before the syscall. The public wrapper is implemented in [libc/src/unistd.c](https://github.com/arlirad/astia/blob/main/libc/src/unistd.c); the kernel syscall entry is [kernel/src/proc/syscalls/process/execve.cpp](https://github.com/arlirad/astia/blob/main/kernel/src/proc/syscalls/process/execve.cpp), and image loading is [kernel/src/elf/exec_loader.cpp](https://github.com/arlirad/astia/blob/main/kernel/src/elf/exec_loader.cpp).
