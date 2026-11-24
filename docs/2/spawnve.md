# spawnve(2)

## SYNOPSIS

```c
long astia_spawnve(const char* path, char* const argv[], char* const envp[]);
```

## DESCRIPTION

Create a process using asynchronous streamed ELF loading.

Copies the vectors, creates a child process, and builds its initial
user stack. The child inherits the caller's descriptors and cwd.

## RETURN VALUE

Returns the child PID, or a negative errno.

## ERRORS

- `-E2BIG`: The argument or environment vector exceeds its limit.
- `-EFAULT`: A path, vector, or string pointer is invalid.
- `-ENOENT`: The executable path does not exist.
- `-EINVAL`: The file is malformed or truncated.
- `-ENOMEM`: The image or process cannot be allocated.
- `-EAGAIN`: The process or thread registry is full.
