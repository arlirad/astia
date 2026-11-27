# setpgid(2)

## SYNOPSIS

```c
long astia_setpgid(int32_t pid, int32_t pgid);
```

## DESCRIPTION

Move a process into a process group.

## RETURN VALUE

Returns zero on success, or a negative errno.

## ERRORS

- `-EINVAL`: Pid or pgid is invalid after special-value handling.
- `-EPERM`: The target is not the caller's child.
- `-EPERM`: The target belongs to another session.
- `-EPERM`: The target is a session leader.
- `-EACCES`: The target is a child that has already executed a new image.
- `-ESRCH`: The target process does not exist.
