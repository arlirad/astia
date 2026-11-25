# thread_exists(2)

## SYNOPSIS

```c
long astia_thread_exists(uint32_t tid);
```

## DESCRIPTION

Checks whether tid identifies a live thread in the calling process.

## RETURN VALUE

Returns one when the thread exists, zero otherwise, or a negative errno.

## ERRORS

- `-ESRCH`: There is no current process.

## NOTES

The query does not acquire ownership of the thread.
