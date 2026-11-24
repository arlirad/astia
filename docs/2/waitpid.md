# waitpid(2)

## SYNOPSIS

```c
long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options);
```

## DESCRIPTION

Selects a child by positive PID, any child with -1, the caller's process group with zero, or a specified process group with a negative PID. It normally blocks until a selected child exits, stops when WUNTRACED is set, or reports a pending continuation when WCONTINUED is set. WNOHANG returns zero when no selected event is ready. An unblocked interrupting signal ends the wait without reaping a child.

## RETURN VALUE

Returns the selected child PID, zero only for WNOHANG with no ready event, or a negative errno. On exit, status uses the traditional layout: signal termination is signal & 0177, normal exit is (exit_status & 0377) << 8, stopped status is (stop_signal << 8) | 0177, and continuation is 0xffff.

## ERRORS

- `-EAGAIN`: The process waiter table cannot accept another waiter.
- `-ECHILD`: The caller has no matching child.
- `-EFAULT`: The status pointer is not writable userspace memory.
- `-EINTR`: An unblocked interrupting signal arrived before a child event.
- `-EINVAL`: The PID or option mask is invalid.
- `-ESRCH`: There is no current process or thread.

## NOTES

This page documents the public ABI wrapper astia_waitpid_options. It returns a negative errno on failure. The libc waitpid() wrapper translates that result to -1 and errno. Without WNOHANG the call may block until a selected child exits, stops, or continues. An unblocked interrupting signal interrupts the wait with -EINTR without reaping a child. If the process waiter table cannot accept another waiter, the call returns -EAGAIN instead of blocking without a wake slot. A successful exit event is reaped. Stop and continuation notifications are consumed but do not reap the child.
