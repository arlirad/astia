# Asynchronous process spawn and `spawnve`

## SYNOPSIS

```c
int spawnve(const char *path, char *const argv[], char *const envp[]);
```

```cpp
core::task<mm::shptr<process>> proc::spawnve(
    process *parent, const mm::shptr<fs::open_file> &file, usize length,
    char *const argv[], char *const envp[]);
```

## DESCRIPTION

The userspace call returns the child PID after the executable has been loaded,
but does not wait for the child to exit. The kernel implementation is the
task-returning `proc::spawnve`; it reads ELF metadata, program segments, and
static TLS through the asynchronous VFS path without allocating a buffer the
size of the image.

The syscall copies the path and complete argument/environment vectors before
starting the task. The child inherits the parent's working directory, file
descriptions, process groups, session, credentials, and filesystem context.
Close-on-exec descriptors are then removed, and the copied vectors are built
on the child's initial stack as `argc`, `argv`, `envp`, and `AT_NULL` entries.

ELF segments and TLS are staged in the child address space. The process is
registered and its user thread is published only after loading and stack setup
complete. A malformed or truncated image releases staged mappings and returns
the loader's invalid-image error; a canceled syscall destroys the task and
also releases every still-pending segment mapping.

## ERRORS

`-E2BIG` reports bounded vector input, `-EFAULT` invalid user pointers,
`-ENOENT` a missing executable, `-EINVAL` a malformed or truncated image,
`-ENOMEM` allocation failure, and `-EAGAIN` process or thread registry
exhaustion. File read failures propagate as `-EIO`.

## REGRESSION COVERAGE

The kernel boot fixture checks argv/envp materialization, malformed truncated
ELF rejection, child completion/reaping, inherited descriptors, fork/exec,
and TLS-backed libc startup. The zsh and nano QEMU launchers exercise repeated
spawn, fork, exec, terminal, descriptor, and editor save paths.
