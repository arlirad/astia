# thread_exit(2)

## SYNOPSIS

```c
void astia_thread_exit(void* result);
```

## DESCRIPTION

Terminate the calling user thread.

Terminates the calling thread and records result for a joining
thread. A process becomes waitable after its last thread exits.

## RETURN VALUE

Does not return.
