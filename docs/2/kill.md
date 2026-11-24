# kill(2)

## SYNOPSIS

```c
long astia_kill(int32_t pid, int32_t signal);
```

## DESCRIPTION

Send a signal to a process or process group

pid > 0 targets one process, pid == 0 targets the caller's
process group, and pid < 0 targets process group -pid. Signal
zero performs an existence check without enqueuing a signal.

## RETURN VALUE

Returns zero on success.

## ERRORS

- `-EINVAL`: Signal or target identifier is malformed.
- `-ESRCH`: No matching live process or process group exists.
