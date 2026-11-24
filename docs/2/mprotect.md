# mprotect(2)

## SYNOPSIS

```c
long astia_mprotect(void* addr, uint64_t len, uint32_t prot);
```

## DESCRIPTION

Validates a requested protection change for a mapped virtual range. Astia does not currently implement page-permission changes.

## RETURN VALUE

Returns a negative errno.

## ERRORS

- `-EINVAL`: The address, length, or protection value is invalid.
- `-ENOSYS`: Protection changes are not implemented.

## NOTES

The syscall number is reserved in the public ABI; mappings retain their creation-time permissions.
