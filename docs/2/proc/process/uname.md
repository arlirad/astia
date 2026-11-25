# uname(2)

## SYNOPSIS

```c
long astia_uname(astia_utsname* output);
```

## DESCRIPTION

Read the system identity.

Copies the kernel's system identification into output. The five
fields are fixed 65-byte arrays; every byte after a field's
terminating NUL is zero.

## RETURN VALUE

Returns zero on success or a negative errno.

## ERRORS

- `-EFAULT`: Output is null or not writable userspace memory.
- `-ENOSYS`: The selected architecture has no machine identity.
- `-ESRCH`: There is no current process.
