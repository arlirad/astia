# io_register(2)

## SYNOPSIS

```c
long astia_io_register(astia_rings* ring, uint32_t opcode, const void* ptr, uint32_t nr);
```

## DESCRIPTION

Register asynchronous I/O resources.

Requests registration of resources for a ring. The ABI defines
REG_FILES and REG_BUFFERS, with UNREG_FILES and UNREG_BUFFERS for
removal. REG_FILES takes an array of int32_t file descriptors;
REG_BUFFERS takes an array of astia_reg_buf records containing an
address and byte length. Registered resources would be selected by
SQF_FIXED_FILE and SQF_FIXED_BUF in SQEs.

## RETURN VALUE

This syscall always fails because resource registration is not
implemented. It does not pin files or buffers and does not change
the ring.

## ERRORS

- `-ENOSYS`: The requested registration or unregistration operation is unsupported by the current kernel.

## NOTES

No registration opcode, pointer, count, or ring is currently
validated because the handler returns -ENOSYS before using them.
