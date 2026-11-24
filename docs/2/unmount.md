# unmount(2)

## SYNOPSIS

```c
long astia_unmount(const char* target);
```

## DESCRIPTION

Removes the native in-memory filesystem mount at target. Only a process with effective UID zero may request teardown. The root mount is permanent; a mount with open file descriptions or child mounts remains busy.

## RETURN VALUE

Returns zero, or a negative errno.

## ERRORS

- `-EFAULT`: Target is not a readable userspace string.
- `-EPERM`: The caller's effective UID is not zero.
- `-ENOENT`: Target is not a mounted target.
- `-EBUSY`: Target is the root mount, has open handles, or contains another mount.

## NOTES

The kernel copies target before authorization and lookup. Unmounting does not flush, persist, or access a host filesystem. The filesystem type remains registered so kernel code may mount another native instance.
