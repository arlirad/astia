# thread_kill(2)

## SYNOPSIS

```c
long astia_thread_kill(uint32_t tid, int32_t signal);
```

## DESCRIPTION

Sends signal to a thread in the calling process.

## RETURN VALUE

Returns zero or a negative errno.

## ERRORS

- `-EINVAL`: Signal is invalid or cannot be delivered.
- `-ESRCH`: The thread does not exist.

## NOTES

Signal delivery follows the thread's process dispositions.
