# sigreturn(2)

## SYNOPSIS

Raw kernel ABI. No public C wrapper declaration is provided for this syscall.

## DESCRIPTION

Restores the signal execution context saved by the kernel before a signal handler ran. The handler returns through this raw syscall, which rewrites the interrupted thread's register and stack image from the saved context.

## RETURN VALUE

Does not return to this call; execution resumes at the restored instruction pointer.

## ERRORS

- `-EFAULT`: The saved context or its registers are not accessible userspace memory.
