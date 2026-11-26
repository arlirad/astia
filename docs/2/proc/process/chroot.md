# chroot(2)

## SYNOPSIS

```c
long astia_chroot(const char* path);
```

## DESCRIPTION

Changes the calling process's filesystem root to the directory named by path. Only a process whose effective user ID is zero may call it. The path is resolved through the process's current root, so a chrooted process can only move its root deeper, never back out.

The working directory is not changed. While it lies outside the new root, relative path resolution fails with `-ENOENT` until the process changes into the root. Absolute paths, `..`, and symlinks are clamped at the new root; a symlink with an absolute target is rebased inside the root.

Descriptors opened before chroot remain usable for I/O on the objects they already reference. A directory descriptor whose canonical path lies outside the new root cannot be used as the base of a new path resolution, and `fchdir` to it fails.

## RETURN VALUE

Returns zero on success, or a negative errno.

## ERRORS

- `-EPERM`: The effective user ID is not zero.
- `-EFAULT`: The path pointer is inaccessible.
- `-ENOENT`: The path or one of its components does not exist.
- `-ENOTDIR`: The path does not identify a directory.
- `-EACCES`: Search permission is denied, or the root would leave the current root.
- `-ELOOP`: Too many symlinks were traversed.
- `-ENAMETOOLONG`: The path or the resolved root exceeds the path limit.
- `-ESRCH`: There is no current process.

## NOTES

The root is process-local, inherited across fork and preserved across exec. As with other filesystem state, the root namespace is the in-memory native VFS; this call never accesses a host filesystem.
