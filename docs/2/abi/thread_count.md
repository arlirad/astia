# thread_count(2)

## SYNOPSIS

```c
long astia_thread_count(void);
```

## DESCRIPTION

Returns the number of live threads in the calling process, including the
calling thread.  A process that has not created threads therefore observes
one.

## RETURN VALUE

Returns the live thread count, or a negative errno.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The count is a snapshot taken by the kernel; another thread may start or
exit immediately after the call.
