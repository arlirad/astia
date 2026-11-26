# modclose(2)

## SYNOPSIS

```c
long astia_modclose(const astia_module_id* id);
```

## DESCRIPTION

Quiesce and unload a published kernel module.

Only a process whose effective user ID is zero may call this operation. The
kernel rejects a running module because its driver must finish teardown and
call the module exit path first. A resident boot module is permanently
non-unloadable. A stale or invalid ID deterministically reports `-ENOENT`.

Otherwise close marks the module quiescing: new references are refused and
active references and dependents block the final unmap. The operation is
task-shaped and waits for those counts to drain through the module close
waiter contract. The final unmap runs in the waiting task, never under the
registry lock, and no code still reachable by a callback, task, or IRQ is
unmapped.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EBUSY`: The module is running, another close owns it, or the module state is not closeable.
- `-EFAULT`: id is null or not accessible userspace memory.
- `-EINVAL`: id carries a nonzero reserved field.
- `-ENOENT`: The ID is invalid, stale, or names no module.
- `-ENOMEM`: The kernel cannot allocate the close waiter.
- `-EPERM`: The effective UID is not zero, or the module is resident.
- `-ESRCH`: There is no current process.

## NOTES

Closing a boot module is always rejected. A rejected close leaves the module
published and usable.
