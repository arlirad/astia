# modopen(2)

## SYNOPSIS

```c
long astia_modopen(const char* path, uint32_t flags, astia_module_id* out);
```

## DESCRIPTION

Load and publish the kernel module image at path.

Only a process whose effective user ID is zero may call this operation. The
path is resolved in the caller's VFS namespace; the target must be a regular
file, readable by the caller, and a valid relocatable kernel module image.
The image size is bounded by kernel policy and the whole image is read before
loading begins.

Relocation, symbol resolution, and dependency pinning all complete before the
module becomes visible. A failure at any point leaves no registry slot behind.
On success out receives a stable generation-checked module ID. The slot is
never compacted and a slot's generation changes when it is released, so a
closed module's ID deterministically reports `-ENOENT` rather than aliasing a
later module.

The operation is task-shaped because filesystem reads can wait. It does not
run the module entry point.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-E2BIG`: The image exceeds the kernel's module image bound.
- `-EACCES`: The caller cannot read the image.
- `-EFAULT`: The path or output pointer is null or not accessible userspace memory.
- `-EINVAL`: flags is nonzero or the path is invalid.
- `-ENOENT`: The path does not name a published module image.
- `-ENOEXEC`: The file is malformed, is not a loadable module image, or is not an ET_REL object.
- `-ENOMEM`: The kernel cannot allocate the load task or image buffer.
- `-ENOSPC`: The registry or image frame allocation is full.
- `-EPERM`: The effective UID is not zero.
- `-ESRCH`: There is no current process.

## NOTES

Boot modules loaded by the kernel remain resident and are unaffected by this
call. A userspace load never creates a resident module. The returned ID carries
no raw kernel pointer.

Only ET_REL relocatable objects are kernel modules. Executable ET_EXEC and
ET_DYN images are rejected with `-ENOEXEC` before loading begins.

If the module is published but the ID cannot be copied out, the kernel
attempts to close it transactionally and returns `-EFAULT`. If that rollback
cannot complete, the failure is reported as `-EIO` rather than silently
leaking a published module.
